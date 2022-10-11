#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

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
  // +1 for '\0'
  memmove(buf, p, strlen(p)+1);
  return buf;
}

void
find(char *path, char *filename) {
    char buf[512], *p;
    struct dirent de;
    struct stat st;
    int fd;

    if((fd = open(path, O_RDONLY)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
    case T_FILE:
        // 对于文件，可以直接比较文件名
        if(strcmp(fmtname(path), filename) == 0) {
            printf("%s\n", path);
        }
        break;
    case T_DIR:
        // 对于目录，则需要处理后递归查找
        // 若路径太长超过最大路径大小，则直接崩掉
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
            fprintf(2, "find: path too long\n");
            break;
        }
        // 在路径后面加上 '/'
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            // 不要递归到当前目录和父目录
            if(de.inum == 0 || (strcmp(de.name, ".") == 0) || (strcmp(de.name, "..") == 0)) {
                continue;
            }
            // 把下级目录加进路径中并递归
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            find(buf, filename);
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if(argc < 3) {
        fprintf(2, "Usage: find [path] [filename]\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}