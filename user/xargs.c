#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]){
    char buf[MAXARG];
    int n = read(0, buf, sizeof buf);
    char* nargv[MAXARG];
    char* p = buf;

    for(int i = argc; i < MAXARG; i++,p++){
        if(*p == '\n'){
            *p = 0;
        }
        nargv[i] = *p;
    }
}
