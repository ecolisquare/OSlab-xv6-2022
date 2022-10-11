#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#define MAX_PRIME 35

void
prime(int *p) {
    // 不用的文件描述符及时关闭，例如此处不需要向管道中写数据，因此直接关闭写一端
    close(p[1]);
    // 从管道中读入第一个数，直接输出并记录
    int primes;
    if(read(p[0], &primes, sizeof(primes)) != sizeof(primes)) {
        fprintf(2, "Error\n");
        exit(1);
    }
    printf("prime %d\n", primes);
    int left;
    // 这里是递归边界，若管道为空，则 read() 会返回 0 代表没有读到任何数据
    if(read(p[0], &left, sizeof(left))) {
        int p1[2];
        pipe(p1);
        if(fork() == 0) {
            // 对子进程，递归的对后面的数进行处理
            prime(p1);
        } else {
            // 对主进程，关闭创建的新管道读一端
            close(p1[0]);
            // 从左侧管道读入数字，如果不能与头一个数整除，则送入右侧管道。
            do {
                if(left % primes != 0) {
                    write(p1[1], &left, sizeof(left));
                }
            }while(read(p[0], &left, sizeof(left)));
            // 读写完毕后关闭管道
            close(p1[1]);
            // 等待子进程结束
            wait(0);
        }
    }
    close(p[0]);
    exit(0);

}

int
main() {
    int p[2];
    int status = 0;
    pipe(p);
    // 创建管道，将 2-35 全部送入管道，fork 出子进程传入管道，调用 prime 函数开始递归
    if(fork() == 0) {
        prime(p);
    } else {
        close(p[0]);
        for(int i = 2;i <= MAX_PRIME;i++) {
            if(write(p[1], &i, sizeof(i)) != sizeof(i)) {
                fprintf(2, "Error\n");
                exit(1);
            }
        }
        close(p[1]);
        wait(&status);
    }
    if(status) {
        exit(0);
    }
    exit(1);
}