#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>

#define VD_SIZE 64 * 1024 * 1024
#define BLOCK_SIZE 1024
#define TOTAL_BLOCKS_INDEX 0
#define FREE_BLOCKS_INDEX 4
#define FIRST_BLOCK_ROOT_INDEX 8
#define FAT_START_BLOCK 9
#define FAT_END_BLOCK 264
#define ROOT_BLOCK 265
#define FAT_ENTRIES_PER_BLOCK 256
#define METADATA_ENTRIES_PER_BLOCK 32

unsigned char* VD_store;

key_t key_VD = -1;
int shmid_VD = -1;

struct metadata {
    char type;
    char name[23];
    unsigned int size;
    unsigned int firstblock;
};

unsigned int NBLOCKS = -1;
unsigned int NFREEBLOCKS = -1;
unsigned int RBN = -1;

void joindisk()
{
    key_VD = ftok("/home",65);
    if(key_VD == -1)
    {
        perror("Key creation error");
        exit(1);
    }

    shmid_VD = shmget(key_VD,VD_SIZE,0666);
    if(shmid_VD == -1)
    {
        perror("SHMID attainment error");
        exit(1);
    }

    VD_store = (unsigned char*)shmat(shmid_VD,NULL,0);
    if(VD_store == (unsigned char*)-1)
    {
        perror("VD_store attachment error");
        exit(1);
    }

    memcpy(&NBLOCKS,VD_store + TOTAL_BLOCKS_INDEX,4);
    memcpy(&NFREEBLOCKS,VD_store + FREE_BLOCKS_INDEX,4);
    memcpy(&RBN,VD_store + FIRST_BLOCK_ROOT_INDEX,4);
}

void leavedisk()
{
    shmdt(VD_store);
}

unsigned int getfreeblock()
{
    unsigned int my_block = rand() % 65536;
    unsigned int my_block_byte = my_block / 8 + 1024;
    unsigned int my_block_bit = 7 - my_block % 8;

    unsigned char mapByte = VD_store[my_block_byte];
    while(mapByte & (1 << my_block_bit))
    {
        my_block = rand() % 65536;
        my_block_byte = my_block / 8 + 1024;
        my_block_bit = 7 - my_block % 8;

        mapByte = VD_store[my_block_byte];
    }

    VD_store[my_block_byte] |= (1 << my_block_bit);

    memcpy(&NFREEBLOCKS,VD_store + FREE_BLOCKS_INDEX,4);
    NFREEBLOCKS--;
    memcpy(VD_store + FREE_BLOCKS_INDEX,&NFREEBLOCKS,4);

    return my_block;
}

void freeblock(unsigned int my_block)
{
    unsigned int my_block_byte = my_block / 8 + 1024;
    unsigned int my_block_bit = 7 - my_block % 8;

    unsigned char mapByte = VD_store[my_block_byte];
    mapByte &= ~(1 << my_block_bit);
    VD_store[my_block_byte] = mapByte;

    memcpy(&NFREEBLOCKS,VD_store + FREE_BLOCKS_INDEX,4);
    NFREEBLOCKS++;
    memcpy(VD_store + FREE_BLOCKS_INDEX,&NFREEBLOCKS,4);

    memset(VD_store + BLOCK_SIZE * my_block,0,BLOCK_SIZE);

    memset(VD_store + FAT_START_BLOCK * BLOCK_SIZE + my_block * 4,0,4);
}

void extract_parts(const char* input,char* path_out,char* name_out)
{
    char* working_path = strdup(input);
    if(working_path == NULL) return;

    char* last_slash = strrchr(working_path,'/');

    if(last_slash != NULL)
    {
        *last_slash = '\0';

        if(strlen(working_path) == 0)
        {
            strcpy(path_out,"/");
        }
        else
        {
            strcpy(path_out,working_path);
        }

        strcpy(name_out,last_slash + 1);
    }
    else
    {
        strcpy(path_out,".");
        strcpy(name_out,working_path);
    }

    free(working_path);
}

unsigned int get_block_from_path(char* input,unsigned int starting_block)
{
    char* input_copy = strdup(input);
    if(input_copy == NULL) return -1;

    char* delimiter = "/";
    char* token = strtok(input_copy,delimiter);

    while(token != NULL)
    {
        int found = 0;
        while(found == 0)
        {
            int start_block_byte = starting_block * BLOCK_SIZE;
            int start_metadata_byte = start_block_byte;

            int start_directory_block = -1;

            int checked = 0;
            while(checked < METADATA_ENTRIES_PER_BLOCK)
            {
                struct metadata md;
                memcpy(&md,VD_store + start_metadata_byte,sizeof(struct metadata));
                if(strcmp(token,md.name) == 0)
                {
                    found = 1;
                    start_directory_block = md.firstblock;
                    break;
                }

                checked++;
                start_metadata_byte += sizeof(struct metadata);
            }

            if(found == 0)
            {
                unsigned int new_starting_block;
                memcpy(&new_starting_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + starting_block * 4,4);

                if(new_starting_block == 0)
                {
                    free(input_copy);
                    return -1;
                }
                starting_block = new_starting_block;
            }

            else
            {
                starting_block = start_directory_block;
            }
        }

        token = strtok(NULL,delimiter);
    }

    free(input_copy);
    return starting_block;
}

int get_starting_byte(int parent_block)
{
    while(1)
    {
        int starting_block_byte = parent_block * BLOCK_SIZE;
        int starting_metadata_byte = starting_block_byte;

        int checked = 0;
        while(checked < METADATA_ENTRIES_PER_BLOCK)
        {
            struct metadata md;
            memcpy(&md,VD_store + starting_metadata_byte,sizeof(struct metadata));
            if(md.type == '\0')
            {
                return starting_metadata_byte;
            }
            checked++;

            starting_metadata_byte += sizeof(struct metadata);
        }

        unsigned int next_block = 0;
        memcpy(&next_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + parent_block * 4,4);
        if(next_block)
        {
            parent_block = next_block;
        }
        else
        {
            unsigned int new_block = getfreeblock();

            memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + parent_block * 4,&new_block,4);
            memset(VD_store + new_block * BLOCK_SIZE,0,BLOCK_SIZE);

            parent_block = new_block;
        }
    }

    return -1;
}

int make_directory(char* input,unsigned int current_dir_block)
{
    char path[1024];
    char name[23];

    extract_parts(input,path,name);

    int start_block = -1;
    if(input[0] == '/')
    {
        start_block = ROOT_BLOCK;
    }
    else 
    {
        start_block = current_dir_block;
    }

    unsigned int parent_block = get_block_from_path(path,start_block);
    if(parent_block == (unsigned int)-1)
    {
        return -1;
    }

    unsigned int new_dir_block = getfreeblock();
    memset(VD_store + new_dir_block * BLOCK_SIZE,0,BLOCK_SIZE);

    struct metadata my_dir;
    my_dir.type = 'd';
    strcpy(my_dir.name,".");
    my_dir.size = 2;
    my_dir.firstblock = new_dir_block;

    memcpy(VD_store + new_dir_block * BLOCK_SIZE,&my_dir,sizeof(struct metadata));

    struct metadata parent_dir;
    parent_dir.type = 'd';
    strcpy(parent_dir.name,"..");
    parent_dir.size = 0;
    parent_dir.firstblock = parent_block;

    memcpy(VD_store + new_dir_block * BLOCK_SIZE + sizeof(struct metadata),&parent_dir,sizeof(struct metadata));

    int my_starting_byte = get_starting_byte(parent_block);

    struct metadata new_dir;
    new_dir.type = 'd';
    strcpy(new_dir.name,name);
    new_dir.size = 2;
    new_dir.firstblock = new_dir_block;

    memcpy(VD_store + my_starting_byte,&new_dir,sizeof(struct metadata));

    struct metadata temp_dir;
    memcpy(&temp_dir,VD_store + parent_block * BLOCK_SIZE,sizeof(struct metadata));
    temp_dir.size++;
    memcpy(VD_store + parent_block * BLOCK_SIZE,&temp_dir,sizeof(struct metadata));

    return 0;
}

int get_byte_from_path(char* input,unsigned int starting_block,int* flag)
{
    char* input_copy = strdup(input);
    if(input_copy == NULL) return -1;

    char* delimiter = "/";
    char* token = strtok(input_copy,delimiter);

    int answer_byte = -1;

    while(token != NULL)
    {
        int found = 0;
        while(found == 0)
        {
            int start_block_byte = starting_block * BLOCK_SIZE;
            int start_metadata_byte = start_block_byte;

            int start_directory_block = -1;

            int checked = 0;
            while(checked < METADATA_ENTRIES_PER_BLOCK)
            {
                struct metadata md;
                memcpy(&md,VD_store + start_metadata_byte,sizeof(struct metadata));
                if(strcmp(token,md.name) == 0)
                {
                    found = 1;
                    start_directory_block = md.firstblock;

                    if(md.type == 'd')
                    {
                        *flag = 1;
                        answer_byte = start_directory_block * BLOCK_SIZE;
                    }
                    else if(md.type == 'f')
                    {
                        *flag = 0;
                        answer_byte = start_metadata_byte;
                    }

                    break;
                }

                checked++;
                start_metadata_byte += sizeof(struct metadata);
            }

            if(found == 0)
            {
                unsigned int new_starting_block;
                memcpy(&new_starting_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + starting_block * 4,4);

                if(new_starting_block == 0)
                {
                    free(input_copy);
                    return -1;
                }
                starting_block = new_starting_block;
            }

            else
            {
                starting_block = start_directory_block;
            }
        }

        token = strtok(NULL,delimiter);
    }

    free(input_copy);
    return answer_byte;
}

int list_starting_byte(char* input,int current_dir_block,int* flag)
{
    if(input == NULL)
    {
        *flag = 1;
        return current_dir_block * BLOCK_SIZE;
    }

    if(strcmp(input,"/") == 0)
    {
        *flag = 1;
        return ROOT_BLOCK * BLOCK_SIZE;
    }

    int input_size = strlen(input);
    if(input[input_size - 1] == '/')
    {
        input[input_size - 1] = '\0';
    }

    int start_block = -1;
    if(input[0] == '/')
    {
        start_block = ROOT_BLOCK;
    }
    else
    {
        start_block = current_dir_block;
    }

    int start_byte = get_byte_from_path(input,start_block,flag);
    return start_byte;
}

int list(char* input,int current_dir_block)
{
    int flag;
    int start_byte = list_starting_byte(input,current_dir_block,&flag);

    if(start_byte == -1)
    {
        printf("***Error: Path not found\n");
        return -1;
    }

    if(flag == 1)
    {
        struct metadata md;
        memcpy(&md,VD_store + start_byte,sizeof(struct metadata));
        printf("Total %u entries\n\n",md.size);
    }

    printf("%-5s %-24s %-8s %s\n", "TYPE", "NAME", "SIZE", "FIRST BLOCK");

    if(flag == 0)
    {
        struct metadata md;
        memcpy(&md,VD_store + start_byte,sizeof(struct metadata));
        printf("%-5c %-24s %-8u %u\n",md.type,md.name,md.size,md.firstblock);
    }
    else if(flag == 1)
    {
        unsigned int current_block = start_byte / BLOCK_SIZE;
        while(current_block != 0)
        {
            int byte_offset = current_block * BLOCK_SIZE;

            for(int i = 0;i < METADATA_ENTRIES_PER_BLOCK;i++)
            {
                struct metadata md;
                memcpy(&md,VD_store + byte_offset,sizeof(struct metadata));

                if(md.type == 'f' || md.type == 'd')
                {
                    if(md.type == 'd')
                    {
                        struct metadata actual_dir_md;
                        memcpy(&actual_dir_md,VD_store + md.firstblock * BLOCK_SIZE,sizeof(struct metadata));

                        char dir_name[25];
                        snprintf(dir_name,sizeof(dir_name),"%s/",md.name);   
                        printf("%-5c %-24s %-8u %u\n",md.type,dir_name,actual_dir_md.size,md.firstblock);
                    }
                    else
                    {
                        printf("%-5c %-24s %-8u %u\n",md.type,md.name,md.size,md.firstblock);
                    }
                }
                byte_offset += sizeof(struct metadata);
            }

            unsigned int next_block;
            memcpy(&next_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + current_block * 4,4);
            current_block = next_block;
        }
    }

    return 0;
}

int copy_HD_to_VD(char* src,char* dst,int current_dir_block)
{
    char src_name[23];
    char* last_slash = strrchr(src,'/');
    if(last_slash != NULL)
    {
        strcpy(src_name,last_slash + 1);
    }
    else
    {
        strcpy(src_name,src);
    }

    FILE* fp = fopen(src,"rb");
    if(fp == NULL)
    {
        printf("***Error: Unable to read input file %s\n",src);
        return -1;
    }

    fseek(fp,0,SEEK_END);
    unsigned int file_size = ftell(fp);
    fseek(fp,0,SEEK_SET);

    int flag;
    int start_byte = list_starting_byte(dst,current_dir_block,&flag);

    unsigned int target_dir_block;
    char target_name[23];

    if(start_byte != -1 && flag == 1)
    {
        target_dir_block = start_byte / BLOCK_SIZE;
        strcpy(target_name,src_name);
    }
    else
    {
        char dest_path[1024];
        extract_parts(dst,dest_path,target_name);

        int p_start = -1;
        if(dst[0] == '/')
        {
            p_start = ROOT_BLOCK;
        }
        else
        {
            p_start = current_dir_block;
        }

        target_dir_block = get_block_from_path(dest_path,p_start);
        
        if(target_dir_block == (unsigned int)-1)
        {
            printf("***Error: Destination path not found\n");
            fclose(fp);
            return -1;
        }
    }

    unsigned int current_block = target_dir_block;
    unsigned int last_block = 0;

    int metadata_byte = -1;
    unsigned int old_first_block = 0;
    int is_new_entry = 0;

    while(current_block != 0)
    {
        int byte_offset = current_block * BLOCK_SIZE;

        for(int i = 0;i < METADATA_ENTRIES_PER_BLOCK;i++)
        {
            struct metadata md;
            memcpy(&md,VD_store + byte_offset,sizeof(struct metadata));

            if(md.type == '\0' && metadata_byte == -1)
            {
                metadata_byte = byte_offset;
            }
            else if(md.type != '\0' && strcmp(md.name,target_name) == 0)
            {
                metadata_byte = byte_offset;
                old_first_block = md.firstblock;
                break;
            }
            byte_offset += sizeof(struct metadata);
        }

        if(old_first_block != 0)
        {
            break;
        }

        last_block = current_block;
        memcpy(&current_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + current_block * 4,4);
    }

    if(old_first_block == 0 && metadata_byte != -1)
    {
        is_new_entry = 1;
    }

    if(metadata_byte == -1)
    {
        unsigned int new_dir_ext = getfreeblock();
        memset(VD_store + new_dir_ext * BLOCK_SIZE,0,BLOCK_SIZE);
        memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + last_block * 4,&new_dir_ext,4);
        metadata_byte = new_dir_ext * BLOCK_SIZE;
        is_new_entry = 1;
    }

    if(old_first_block != 0)
    {
        unsigned int b_to_free = old_first_block;
        while(b_to_free != 0)
        {
            unsigned int next_b;
            memcpy(&next_b,VD_store + FAT_START_BLOCK * BLOCK_SIZE + b_to_free * 4,4);
            freeblock(b_to_free);
            b_to_free = next_b;
        }
    }

    unsigned int new_first_block = 0;
    unsigned int prev_block = 0;
    unsigned int bytes_remaining = file_size;

    if(bytes_remaining == 0)
    {
        new_first_block = getfreeblock();
        unsigned int end_marker = 0;
        memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + new_first_block * 4,&end_marker,4);
    }

    while(bytes_remaining > 0)
    {
        unsigned int curr_b = getfreeblock();
        if(new_first_block == 0)
        {
            new_first_block = curr_b;
        }

        if(prev_block != 0)
        {
            memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + prev_block * 4,&curr_b,4);
        }

        unsigned int chunk = (bytes_remaining > BLOCK_SIZE) ? BLOCK_SIZE : bytes_remaining;
        fread(VD_store + curr_b * BLOCK_SIZE,1,chunk,fp);
        
        bytes_remaining -= chunk;
        prev_block = curr_b;
    }

    if(prev_block != 0)
    {
        unsigned int end_fat = 0;
        memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + prev_block * 4,&end_fat,4);
    }

    fclose(fp);

    struct metadata final_md;
    final_md.type = 'f';
    strcpy(final_md.name,target_name);
    final_md.size = file_size;
    final_md.firstblock = new_first_block;
    memcpy(VD_store + metadata_byte,&final_md,sizeof(struct metadata));

    if(is_new_entry == 1)
    {
        struct metadata dot_md;
        memcpy(&dot_md,VD_store + target_dir_block * BLOCK_SIZE,sizeof(struct metadata));
        dot_md.size++;
        memcpy(VD_store + target_dir_block * BLOCK_SIZE,&dot_md,sizeof(struct metadata));
    }

    return 0;
}

int copy_VD_to_HD(char* src,char* dst,int current_dir_block)
{
    int flag;
    int start_byte = list_starting_byte(src,current_dir_block,&flag);

    if(start_byte == -1 || flag == 1)
    {
        return -1;
    }

    struct metadata md;
    memcpy(&md,VD_store + start_byte,sizeof(struct metadata));

    FILE* fp = fopen(dst,"wb");
    if(fp == NULL)
    {
        return -1;
    }

    unsigned int current_block = md.firstblock;
    unsigned int bytes_remaining = md.size;

    while(current_block != 0 && bytes_remaining > 0)
    {
        unsigned int chunk = (bytes_remaining > BLOCK_SIZE) ? BLOCK_SIZE : bytes_remaining;
        fwrite(VD_store + current_block * BLOCK_SIZE,1,chunk,fp);
        bytes_remaining -= chunk;

        unsigned int next_block;
        memcpy(&next_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + current_block * 4,4);
        current_block = next_block;
    }

    fclose(fp);
    return 0;
}

int copy_VD_to_VD(char* src,char* dst,int current_dir_block)
{
    int src_flag;
    int src_start_byte = list_starting_byte(src,current_dir_block,&src_flag);

    if(src_start_byte == -1 || src_flag == 1)
    {
        return -1;
    }

    struct metadata src_md;
    memcpy(&src_md,VD_store + src_start_byte,sizeof(struct metadata));

    int dst_flag;
    int dst_start_byte = list_starting_byte(dst,current_dir_block,&dst_flag);

    unsigned int target_dir_block;
    char target_name[23];

    if(dst_start_byte != -1 && dst_flag == 1)
    {
        target_dir_block = dst_start_byte / BLOCK_SIZE;
        strcpy(target_name,src_md.name);
    }
    else
    {
        char dest_path[1024];
        extract_parts(dst,dest_path,target_name);

        int p_start = -1;
        if(dst[0] == '/')
        {
            p_start = ROOT_BLOCK;
        }
        else
        {
            p_start = current_dir_block;
        }

        target_dir_block = get_block_from_path(dest_path,p_start);
        
        if(target_dir_block == (unsigned int)-1)
        {
            return -1;
        }
    }

    unsigned int current_block = target_dir_block;
    unsigned int last_block = 0;

    int metadata_byte = -1;
    unsigned int old_first_block = 0;
    int is_new_entry = 0;

    while(current_block != 0)
    {
        int byte_offset = current_block * BLOCK_SIZE;

        for(int i = 0;i < METADATA_ENTRIES_PER_BLOCK;i++)
        {
            struct metadata md;
            memcpy(&md,VD_store + byte_offset,sizeof(struct metadata));

            if(md.type == '\0' && metadata_byte == -1)
            {
                metadata_byte = byte_offset;
            }
            else if(md.type != '\0' && strcmp(md.name,target_name) == 0)
            {
                metadata_byte = byte_offset;
                old_first_block = md.firstblock;
                break;
            }
            byte_offset += sizeof(struct metadata);
        }

        if(old_first_block != 0)
        {
            break;
        }

        last_block = current_block;
        memcpy(&current_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + current_block * 4,4);
    }

    if(old_first_block == 0 && metadata_byte != -1)
    {
        is_new_entry = 1;
    }

    if(metadata_byte == -1)
    {
        unsigned int new_dir_ext = getfreeblock();
        memset(VD_store + new_dir_ext * BLOCK_SIZE,0,BLOCK_SIZE);
        memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + last_block * 4,&new_dir_ext,4);
        metadata_byte = new_dir_ext * BLOCK_SIZE;
        is_new_entry = 1;
    }

    if(old_first_block != 0)
    {
        unsigned int b_to_free = old_first_block;
        while(b_to_free != 0)
        {
            unsigned int next_b;
            memcpy(&next_b,VD_store + FAT_START_BLOCK * BLOCK_SIZE + b_to_free * 4,4);
            freeblock(b_to_free);
            b_to_free = next_b;
        }
    }

    unsigned int new_first_block = 0;
    unsigned int prev_block = 0;
    unsigned int bytes_remaining = src_md.size;
    unsigned int src_current_block = src_md.firstblock;

    if(bytes_remaining == 0)
    {
        new_first_block = getfreeblock();
        unsigned int end_marker = 0;
        memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + new_first_block * 4,&end_marker,4);
    }

    while(bytes_remaining > 0 && src_current_block != 0)
    {
        unsigned int curr_b = getfreeblock();
        if(new_first_block == 0)
        {
            new_first_block = curr_b;
        }

        if(prev_block != 0)
        {
            memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + prev_block * 4,&curr_b,4);
        }

        unsigned int chunk = (bytes_remaining > BLOCK_SIZE) ? BLOCK_SIZE : bytes_remaining;
        memcpy(VD_store + curr_b * BLOCK_SIZE,VD_store + src_current_block * BLOCK_SIZE,chunk);
        
        bytes_remaining -= chunk;
        prev_block = curr_b;

        unsigned int next_src_block;
        memcpy(&next_src_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + src_current_block * 4,4);
        src_current_block = next_src_block;
    }

    if(prev_block != 0)
    {
        unsigned int end_fat = 0;
        memcpy(VD_store + FAT_START_BLOCK * BLOCK_SIZE + prev_block * 4,&end_fat,4);
    }

    struct metadata final_md;
    final_md.type = 'f';
    strcpy(final_md.name,target_name);
    final_md.size = src_md.size;
    final_md.firstblock = new_first_block;
    memcpy(VD_store + metadata_byte,&final_md,sizeof(struct metadata));

    if(is_new_entry == 1)
    {
        struct metadata dot_md;
        memcpy(&dot_md,VD_store + target_dir_block * BLOCK_SIZE,sizeof(struct metadata));
        dot_md.size++;
        memcpy(VD_store + target_dir_block * BLOCK_SIZE,&dot_md,sizeof(struct metadata));
    }

    return 0;
}

int copy_file(char* source_path,char* dest_path,int current_dir_block)
{
    int is_src_HD = (source_path[0] == '`');
    int is_dest_HD = (dest_path[0] == '`');

    if(is_src_HD && !is_dest_HD)
    {
        return copy_HD_to_VD(source_path + 1,dest_path,current_dir_block);
    }
    else if(!is_src_HD && is_dest_HD)
    {
        return copy_VD_to_HD(source_path,dest_path + 1,current_dir_block);
    }
    else if(!is_src_HD && !is_dest_HD)
    {
        return copy_VD_to_VD(source_path,dest_path,current_dir_block);
    }
    else 
    {
        return -1;
    }
}

int dir(int current_dir_block)
{
    unsigned int start_block = current_dir_block;

    while(start_block != 0)
    {
        int byte_offset = start_block * BLOCK_SIZE;

        for(int i = 0;i < METADATA_ENTRIES_PER_BLOCK;i++)
        {
            struct metadata md;
            memcpy(&md,VD_store + byte_offset,sizeof(struct metadata));
            
            if(md.type == 'f' || md.type == 'd')
            {
                if(md.type == 'd')
                {
                    printf("%s/\n", md.name);
                }
                else
                {
                    printf("%s\n", md.name);
                }
            }

            byte_offset += sizeof(struct metadata);
        }

        unsigned int next_block;
        memcpy(&next_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + start_block * 4,4);
        start_block = next_block;
    }

    return 0;
}

int print(char* input,int current_dir_block)
{
    char path[1024];
    char name[23];

    extract_parts(input,path,name);

    int start_block = -1;
    if(input[0] == '/')
    {
        start_block = ROOT_BLOCK;
    }
    else 
    {
        start_block = current_dir_block;
    }

    unsigned int parent_block = get_block_from_path(path,start_block);
    if(parent_block == (unsigned int)-1)
    {
        printf("***Error: Path not found\n");
        return -1;
    }

    unsigned int file_first_block = 0;
    unsigned int print_length = 0;
    int found = 0;

    unsigned int current_block = parent_block;

    while(current_block != 0)
    {
        int byte_offset = current_block * BLOCK_SIZE;

        for(int i = 0;i < METADATA_ENTRIES_PER_BLOCK;i++)
        {
            struct metadata md;
            memcpy(&md,VD_store + byte_offset,sizeof(struct metadata));

            if(md.type == 'f' && strcmp(md.name,name) == 0)
            {
                file_first_block = md.firstblock;
                print_length = md.size;
                found = 1;
                break;
            }

            byte_offset += sizeof(struct metadata);
        }

        if(found == 1)
        {
            break;
        }

        unsigned int next_block;
        memcpy(&next_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + current_block * 4,4);
        current_block = next_block;
    }

    if(found == 0)
    {
        printf("***Error: File '%s' not found or is a directory\n",name);
        return -1;
    }

    unsigned int curr_file_block = file_first_block;
    unsigned int bytes_remaining = print_length;

    while(curr_file_block != 0 && bytes_remaining > 0)
    {
        unsigned int chunk = (bytes_remaining > BLOCK_SIZE) ? BLOCK_SIZE : bytes_remaining;

        fwrite(VD_store + curr_file_block * BLOCK_SIZE,1,chunk,stdout);
        bytes_remaining -= chunk;

        unsigned int next_block;
        memcpy(&next_block,VD_store + FAT_START_BLOCK * BLOCK_SIZE + curr_file_block * 4,4);
        curr_file_block = next_block;
    }

    printf("\n");

    return 0;
}

int change_directory(char* input,int current_dir_block)
{
    if(input == NULL || strlen(input) == 0)
    {
        return ROOT_BLOCK;
    }

    int flag;
    int target_byte = list_starting_byte(input,current_dir_block,&flag);

    if(target_byte == -1)
    {
        printf("***Error: unable to change to directory %s\n", input);
        return -1;
    }

    if(flag == 0)
    {
        printf("***Error: unable to change to directory %s\n", input);
        return -1;
    }

    return target_byte / BLOCK_SIZE;
}