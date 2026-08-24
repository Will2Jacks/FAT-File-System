#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define ROOT_BLOCK 265
extern unsigned int NBLOCKS;
extern unsigned int NFREEBLOCKS;
extern unsigned int RBN;

extern void joindisk();
extern void leavedisk();
extern int make_directory(char* input,unsigned int current_dir_block);
extern int list(char* input,int current_dir_block);
extern int change_directory(char* input,int current_dir_block);
extern int copy_file(char* source_path,char* dest_path,int current_dir_block);
extern int print(char* input,int current_dir_block);
extern int dir(int current_dir_block);

void update_cwd(char* cwd,const char* arg)
{
    if(arg == NULL || strlen(arg) == 0)
    {
        strcpy(cwd,"/");
        return;
    }

    char temp[2048];
    if(arg[0] == '/')
    {
        strcpy(temp,arg);
    }
    else
    {
        strcpy(temp,cwd);
        if(strcmp(temp,"/") != 0)
        {
            strcat(temp,"/");
        }
        strcat(temp,arg);
    }

    char* parts[256];
    int count = 0;

    char* token = strtok(temp,"/");
    while(token != NULL)
    {
        if(strcmp(token,".") == 0)
        {

        }
        else if(strcmp(token,"..") == 0)
        {
            if(count > 0)
            {
                count--;
            }
        }
        else
        {
            parts[count++] = token;
        }
        token = strtok(NULL,"/");
    }

    cwd[0] = '\0';
    if(count == 0)
    {
        strcpy(cwd,"/");
    }
    else
    {
        for(int i = 0;i < count;i++)
        {
            strcat(cwd,"/");
            strcat(cwd,parts[i]);
        }
    }
}

int main()
{
    srand(time(NULL));

    int CBN = ROOT_BLOCK;
    char CWD[1024] = "/";

    joindisk();
    printf("+++ Number of blocks = %u\n",NBLOCKS);
    printf("+++ Number of free blocks = %u\n",NFREEBLOCKS);
    printf("+++ First block of the root directory = %u\n",RBN);

    char line[2048];

    while(1)
    {
        if(strcmp(CWD,"/") == 0)
        {
            printf("[foosh] VD:> ");
        }
        else
        {
            printf("[foosh] VD:%s> ",CWD);
        }
        fflush(stdout);

        if(fgets(line,sizeof(line),stdin) == NULL)
        {
            break;
        }
        line[strcspn(line,"\n")] = '\0';

        if(strlen(line) == 0)
        {
            continue;
        }

        char* cmd = strtok(line," \t");
        if(cmd == NULL)
        {
            continue;
        }

        if(strcmp(cmd,"exit") == 0 || strcmp(cmd,"quit") == 0)
        {
            printf("+++ Number of free blocks = %u\n",NFREEBLOCKS);
            break;
        }
        else if(strcmp(cmd,"ls") == 0)
        {
            char* arg = strtok(NULL," \t");
            list(arg,CBN);
        }
        else if(strcmp(cmd,"dir") == 0)
        {
            dir(CBN);
        }
        else if(strcmp(cmd,"cd") == 0 || strcmp(cmd,"chdir") == 0)
        {
            char* arg = strtok(NULL," \t");

            if(arg != NULL && strlen(arg) > 1 && arg[strlen(arg) - 1] == '/')
            {
                arg[strlen(arg) - 1] = '\0';
            }

            char original_arg[1024] = "";
            if(arg != NULL) 
            {
                strcpy(original_arg,arg);
            }

            int new_block = change_directory(arg,CBN);
            if(new_block != -1)
            {
                CBN = new_block;
                update_cwd(CWD,arg != NULL ? original_arg : NULL);
            }
        }
        else if(strcmp(cmd,"md") == 0 || strcmp(cmd,"mkdir") == 0)
        {
            char* arg = strtok(NULL," \t");
            if(arg != NULL)
            {
                make_directory(arg,CBN);
            }
            else 
            {
                printf("***Error: md requires a directory name\n");
            }
        }
        else if (strcmp(cmd,"prn") == 0 || strcmp(cmd,"type") == 0)
        {
            char* arg = strtok(NULL," \t");
            if (arg != NULL) 
            {
                print(arg,CBN);
            } 
            else 
            {
                printf("***Error: %s requires a file name\n",cmd);
            }
        }
        else if (strcmp(cmd,"cp") == 0 || strcmp(cmd,"copy") == 0) 
        {
            char* src = strtok(NULL," \t");
            char* dst = strtok(NULL," \t");

            if (src != NULL && dst != NULL) 
            {
                copy_file(src,dst,CBN);
            } 
            else 
            {
                printf("***Error: cp requires source and destination arguments\n");
            }
        }
        else 
        {
            printf("***Error: Unknown command '%s'\n",cmd);
        }
    }

    leavedisk();

    return 0;
}