#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

int main(int argc, char* argv[]){
    int pid = fork();
    char c;
    if(pid == 0){
        c = '/';
    }else{
        printf("parent pid is %d，child is %d\n", getpid(), pid);
        c = '\\';
    }
    for(int i = 0; ;i++){
        if((i % 100000) == 0){
            write(2, &c, 1);
        }
    }
    exit(0);
}