#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RD 0
#define WR 1
// 从左端管道读取第一个数据(用if)
int lpipe_first_data(int lpipe[2], int* dest){
    // 读取数据到dest所指的区域,通过管道读取
    if(read(lpipe[RD], dest, sizeof(int)) == sizeof(int))
    {
        printf("prime %d\n", *dest);
        return 0;
    }
    return -1;
}

void transmit_data(int lpipe[2], int rpipe[2], int first){
    // 在getprimes已经关闭了
    // close(lpipe[WR]);
    // close(rpipe[RD]);
    int num;
    while(read(lpipe[RD], &num, sizeof(int)) == sizeof(int)){
        // *num是质数,往右管道传
        if(num % first != 0){
            write(rpipe[WR], &num, sizeof(int));
        }
    }
    close(lpipe[RD]);
    close(rpipe[WR]);
}

void get_primes(int lpipe[2]){
    close(lpipe[WR]);
    int first;
    if(lpipe_first_data(lpipe, &first) == 0){
        int p[2];
        pipe(p);
        transmit_data(lpipe, p, first);
        if(fork() == 0){
            // 写这里父进程把p管道关闭了傻叉，还传个毛数据
            // transmit_data(lpipe, p, first);
            get_primes(p);
            close(p[RD]);
        }else{
            close(p[RD]);
        }
    }
    exit(0);
}

int main(){
    int p[2];
    pipe(p);
    if(fork() == 0){
        for(int i = 2; i <= 35; i++){
            int num = i;
            write(p[WR], &num, sizeof(int));
        }
        get_primes(p);
    }else{
        // 这里不能跟wait交换,这里的文件描述符是父进程的，与子进程无关！不会影响子进程读取数据
        close(p[RD]);
        close(p[WR]);
        wait(0);
    }
    exit(0);
}