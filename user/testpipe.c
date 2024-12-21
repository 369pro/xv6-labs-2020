#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char* argv[]){
    int p[2];
    int res = pipe(p);
    printf("%d\n", res);
    exit(0);
}