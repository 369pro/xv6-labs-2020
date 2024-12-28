#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RD 0
#define WR 1

void transmit_data(int lpipe[], int rpipe[], int prime){
    int num;
    while(read(lpipe[RD], &num, sizeof(int)) == sizeof(int)){
        if(num % prime != 0){
            write(rpipe[WR], &num, sizeof(int));
        }
    }
    close(lpipe[RD]);
    close(rpipe[WR]);
}
/**
 * return 0 success
 * return -1 error
 */
int get_lpipe_first(int lpipe[], int* dest){
    if(read(lpipe[RD], dest, sizeof(int)) == sizeof(int)){
        printf("prime %d\n", *dest);
        return 0;
    }
    return -1;
}

// 从left_pipe读取数据, 从right_pipe写入数据
void get_primes(int lpipe[]){
    close(lpipe[WR]);
    int prime;
    // Q1 终止条件
    if(get_lpipe_first(lpipe, &prime) == 0){
        // Q2 这个现在不能关, 关了的话fork会复制文件描述符,导致子进程用不了这个管道
        // close(lpipe[RD]);
        if(fork() == 0){
            close(lpipe[WR]);
            int rpipe[2];
            pipe(rpipe);
            transmit_data(lpipe, rpipe, prime);
            // Q3 close(rpipe[RD]);
            get_primes(rpipe);
            // Q3这里最后关闭,否则get_primes下一层无法从rpipe里面读取数据
            close(rpipe[RD]);
        }else{
            // Q2 在这里关闭
            close(lpipe[RD]);
        }
    }
    // Q1 没有终止条件，会无限递归
    // if(fork() == 0){
    //     if(get_lpipe_first(lpipe, &prime) == 0){
    //         int rpipe[2];
    //         pipe(rpipe);
    //         transmit_data(lpipe, rpipe, prime);
    //         get_primes(rpipe);
    //         close(rpipe[RD]);
    //         close(rpipe[WR]);
    //     }
    // }
    exit(0);
}

int main(){
    int p[2];
    pipe(p);
    // 子进程负责传输数据,父进程等待
    if(fork() == 0){
        for(int i = 2; i <= 35; i++){
            int num = i;
            write(p[WR], &num, sizeof(int));
        }
        get_primes(p);
    }else{
        // 关闭父进程管道(父进程不用传输数据)
        close(p[RD]);
        close(p[WR]);
        // wait(0)使父进程等待子进程结束，0表示不关心子进程的退出状态。
        wait(0);
    }
    exit(0);
}