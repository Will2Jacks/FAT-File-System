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

#define CREATE 0
#define REMOVAL 1

unsigned char* VD_store;

int mode = -1;

key_t key_VD = -1;
int shmid_VD = -1;

struct metadata {
    char type;
    char name[23];
    unsigned int size;
    unsigned int firstblock;
};

int main(int argc,char* argv[])
{
    if(argc < 2)
    {
        perror("Usage: ./<executable> <mode>");
        exit(1);
    }

    mode = atoi(argv[1]);

    if(mode == CREATE)
    {
        key_VD = ftok("/home",65);
        if(key_VD == -1)
        {
            perror("Key creation error");
            exit(1);
        }

        shmid_VD = shmget(key_VD,VD_SIZE,IPC_CREAT | 0666);
        if(shmid_VD == -1)
        {
            perror("SHMID creation error");
            exit(1);
        }

        VD_store = (unsigned char*)shmat(shmid_VD,NULL,0);
        if(VD_store == (unsigned char*)-1)
        {
            perror("VD_store attachment error");
            exit(1);
        }
        memset(VD_store,0,VD_SIZE);

        unsigned int total_blocks = 65536;
        unsigned int free_blocks = 65270;
        unsigned int first_block_root = 265;

        memcpy(VD_store + TOTAL_BLOCKS_INDEX,&total_blocks,4);
        memcpy(VD_store + FREE_BLOCKS_INDEX,&free_blocks,4);
        memcpy(VD_store + FIRST_BLOCK_ROOT_INDEX,&first_block_root,4);

        unsigned char val = 255;
        for(int i = 1024;i <= 1056;i++)
        {
            memcpy(VD_store + i,&val,1);
        }

        val = 192;
        memcpy(VD_store + 1057,&val,1);
        memset(VD_store + 1058,0,8158);

        unsigned int fat_end_byte = FAT_END_BLOCK * BLOCK_SIZE - 1;
        unsigned int fat_start_byte = FAT_START_BLOCK * BLOCK_SIZE;
        unsigned int fat_size = fat_end_byte - fat_start_byte + 1;

        memset(VD_store + fat_start_byte,0,fat_size);

        struct metadata my_dir;
        my_dir.type = 'd';
        strcpy(my_dir.name,".");
        my_dir.size = 2;
        my_dir.firstblock = ROOT_BLOCK;

        memcpy(VD_store + ROOT_BLOCK * BLOCK_SIZE,&my_dir,sizeof(struct metadata));

        struct metadata parent_dir;
        parent_dir.type = 'd';
        strcpy(parent_dir.name,"..");
        parent_dir.size = 2;
        parent_dir.firstblock = ROOT_BLOCK;

        memcpy(VD_store + ROOT_BLOCK * BLOCK_SIZE + sizeof(struct metadata),&parent_dir,sizeof(struct metadata));
    }

    else
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
            perror("SHMID creation error");
            exit(1);
        }

        if(shmctl(shmid_VD,IPC_RMID,NULL) < 0)
        {
            perror("Removal error");
            exit(1);
        }
    }

    return 0;
}