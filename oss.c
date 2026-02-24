// Have oss create shared memory with a clock, 
//then fork off one child and have that child access the shared memory and
//ensure it works.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>

const size_t BUFF_SZ = sizeof(int) * 2; 
// had to change int to size_t to avoid compiler warning 
//about comparison between signed and unsigned integers 

int main() {
    key_t shm_key = ftok("oss.c", 0);
    //int shm_key changed to key_t 
    //to match the return type of ftok, 
    //and avoid compiler warning about 
    //comparison between signed and unsigned integers
    if (shm_key == (key_t)-1) {
		fprintf(stderr,"OSC:... Error in ftok\n"); 
        exit(1);
    }

    int shm_id = shmget(shm_key, BUFF_SZ, IPC_CREAT | 0700);
    if (shm_id == -1) {
		fprintf(stderr,"OSC:... Error in shmget\n");
        exit(1);
    }

    int *clock = (int *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
		fprintf(stderr,"OSC:... Error in shmat\n");
        exit(1);
    }

    // init clock values to 0,0
    int *sec  = &(clock[0]);
    int *nano = &(clock[1]);
    *sec = *nano = 0;

    printf("OSS PID:%d PPID:%d\n", getpid(), getppid());
    printf("OSS initialized clock: sec=%d nano=%d\n", clock[0], clock[1]);

    pid_t child_pid = fork();
    if (child_pid == -1) {
        // fork failed and we clean up before exiting
		fprintf(stderr,"OSC:... Error in fork\n");
        shmdt(clock);
        shmctl(shm_id, IPC_RMID, NULL);
        exit(1);
    }

    // launch worker
    if (child_pid == 0) {
        // in child
            char *args[] = {"./worker", 0};        
            execlp(args[0], args[0], (char *)0);
    		fprintf(stderr,"Error in exec after fork\n");
            exit(1);
    }

    wait(NULL);

    printf("OSS after child: sec=%d nano=%d\n", *sec, *nano);

    shmdt(clock);
    shmctl(shm_id, IPC_RMID, NULL);
    return 0;
}