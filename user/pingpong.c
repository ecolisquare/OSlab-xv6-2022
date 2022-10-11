#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char buf[100];	
    int p[2], pid;
    if(pipe(p) == -1) printf("fail to create a pipe!\n");
    if (fork() == 0)
    {
        pid = getpid();
        if (read(p[0], buf, 1) == 1)
        {
            printf("%d: received ping\n", pid);
            write(p[1], buf, 1);
        }
    }
    else
    {
        pid = getpid();
        write(p[1], buf, 1);
        if (read(p[0], buf, 1) == 1)
        {
            printf("%d: received pong\n", pid);
        }
    }
    exit(1);
}