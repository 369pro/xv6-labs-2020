#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9.
char buf[1024];
int match(char*, char*);
int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}
// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  if(re[1] == '*')
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
  do{  // a * matches zero or more instances
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}


/*
  find.c
*/
char* fmtname(char *path);
void find(char *path, char* f_name);

int main(int argc, char* argv[]){
    if(argc != 3){
        printf("parameters should be 3\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memset(buf, 0, sizeof(buf));
  memmove(buf, p, strlen(p));
  return buf;
}

void find(char* path, char* f_name){
    char buf[512], *p;
    int fd;
    // 目录中的一个目录项
    struct dirent de;
    // 存储文件状态信息
    struct stat st;
    // 0表示只读模式打开
    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        // 此处打开了文件自然要关闭fd
        close(fd);
        return;
    }

    switch(st.type){
    case T_FILE:
        if(match(f_name, fmtname(path))){
            printf("%s\n", path);
        }
        break;
    
    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
          printf("find: path too long\n");
          break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        // p指向当前目录后面
        *p++ = '/';
        // 从打开的目录文件中读取目录项，并处理每一个读取到的目录项
        // 直到到达文件末尾
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            // 读取的是空目录项
            if(de.inum == 0)
                 continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if(stat(buf, &st) < 0){
                printf("find: cannot stat %s\n", buf);
                continue;
            }
            char* last_name = fmtname(buf);
            if(strcmp(last_name, ".") == 0 || strcmp(last_name, "..") == 0){
                 continue;
            }
            find(buf, f_name);
        }
        break;
    }//switch
    close(fd);
}
