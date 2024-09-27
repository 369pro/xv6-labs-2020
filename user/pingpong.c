#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char* argv[]){
    char buf[1];
    // p1:父-->子， p2:子-->父
    int p1[2], p2[2];
    pipe(p1);
    pipe(p2);
    // 子进程读取父进程数据;
    if(fork() == 0){
        close(p1[1]);
        close(p2[0]);
        read(p1[0], buf, 1);
        close(p1[0]);
        printf("%d: received ping\n", getpid());
        // 发送数据给父进程
        write(p2[1], "b", 1);
        close(p2[1]);
        exit(0);
    }else{
        close(p1[0]);
        close(p2[1]);
        // 父进程向子进程发送一个字节
        write(p1[1], "a", 1);
        close(p1[1]);
        // 等待子进程发送消息
        // wait(NULL);
        // 父进程读取子进程数据并打印信息
        read(p2[0], buf, 1);
        close(p2[0]);
        printf("%d: received pong\n", getpid());
        exit(0);
    }
}
