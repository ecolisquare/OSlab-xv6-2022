#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"
#define NULL ((void *)(0))
#define EOF (-1)

int
getchar() {
    char buf;
    if(read(0, &buf, sizeof(char))) {
        return buf;
    } else {
        return EOF;
    }
}

char *
gets_s(char *s, int size) {
    int c = getchar();
    // 如果读到 EOF，则直接返回空指针
    if(c == EOF) {
        return NULL;
    }

    int i = 0;
    do {
        // 与正经的标准库函数不同，我们的实现由于根本不存在缓冲区，因此也不存在将 `\n' 继续留在缓冲区的说法，即下一次 getchar() 不会读到上一个 '\n'
        // 这种实现更接近于算法比赛的快速读入
        if(c == '\n') {
            break;
        }
        s[i] = c;
        // 如果达到最大大小，提前终止读入
        i++;
        if(i == size) {
            break;
        }
    }while((c = getchar()) != EOF);
    // 补上字符串的 '\0'
    s[i] = 0;
    return s;
}

int
main(int argc, char *argv[]) {
    int status, i;
    
    if(argc < 2) {
        fprintf(2, "Usage: xargs [command]\n");
        exit(0);
    }

    // 缓冲区，用于 exec 的参数
    char *buf[MAXARG+1];

    // argv 是一个 char 类型的指针的数组
    // 在运行时，argv 就已经确定，并且存储在 main() 的栈之前
    // 直接拷贝指针的值避免了拷贝内存的开销
    // argv 不保证连续，最好不要将 argv 作为一个整体内存去拷贝
    for(i = 1;i < argc;i++) {
        buf[i - 1] = argv[i];
    }

    // buf2 作为读缓冲区使用
    char buf2[1024];
    int args = argc - 1;
    while((gets_s(buf2, 1024)) != NULL) {
            int j = 0;
            int l = 0;
            // 处理掉可能存在的前导空格
            for(l = j; buf2[l] == ' ';l++) {
                ;
            }
            j = l;

            for(int k = j; ;k++) {
                // 再次处理空格分割
                if(buf2[k] == '\0') {
                    char *s1 = malloc((k-j+1)*sizeof(char));
                    memcpy(s1, buf2 + j, k - j);
                    s1[k - j + 1] = '\0';
                    buf[args++] = s1;
                    if(args == MAXARG - 1) {
                        fprintf(2, "xargs: Too many arguments...\n");
                        exit(1);
                    }
                    for(; buf2[k] != 0 || buf2[k] == ' ';k++) {
                        ;
                    }
                    j = k;
                }
                if(buf2[k] == '\0') {
                    break;
                }
            }
    }

    // argv 以 NULL 或者说 0 作为结束标志
    buf[args] = 0;

    // fork 一个子进程执行命令，主进程等待子进程执行完毕
    if(fork() == 0) {
        exec(buf[0], buf); 
    } else {    
        wait(&status);
        if(status == 0) {
            exit(0);
        } else {
            exit(1);
        }
    }
    exit(0);
}