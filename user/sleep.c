#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
	int time;
	if (argc < 2){
		printf("sleep error\n");
		exit(1);
	}
	time = atoi(argv[1]);
	if (time > 0){
		sleep(time);
		printf("(nothing happens for a little while)\n");
	} else {
		printf("Invalid interval %s\n", argv[1]);
	}
	exit(1);
}
