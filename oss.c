#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <getopt.h> 

const size_t BUFF_SZ = sizeof(int) * 2; 
// had to change int to size_t to avoid compiler warning 
//about comparison between signed and unsigned integers 

int main(int argc, char *argv[]) {
    // initialize variables for options
    int n = 1;  
    int s = 1;  
    float t = 1.0f; 
    float i = 0.0f;

    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1) { //parse CL options using getopt
        switch (opt) {
            case 'h':
                printf("oss [-h help] [-n processes] [-s simulations] [-t time] [-i interval]\n");
                return 0;
            case 'n':
                n = atoi(optarg);
                break;
            case 's':
                s = atoi(optarg);
                break;
            case 't':
                //strtof converts string to float, 
                //and we use NULL for the second argument since we don't need it
                //better than atof since it provides error checking
                t = strtof(optarg, NULL);
                break;
            case 'i':
                i = strtof(optarg, NULL);
                break;
            default:
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t iter]\n", argv[0]);
                return 1;
        }
    } 
    //avoid invalid input
    if (n <= 0) n = 1;
    if (s <= 0) s = 1;
    if (t <= 0) t = 1.0f;
    if (i <= 0) i = 0.0f;
    if (s > n) s = n; //we cannot have more simul processes than total processes  
    const int NANOPERSEC = 1000000000; //1 billion nanoseconds in a second
    const int INCREMENTNANO = 10000000; //10ms in nanoseconds;

    key_t shm_key = ftok("oss.c", 0);
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

    printf("OSS starting, PID:%d PPID:%d\n", getpid(), getppid());
    //At the start of oss, print out the parameters passed to it.
    printf("Called with:\n-n %d\n-s %d\n-t %.3f\n-i %.3f\n", n, s, t, i);
    printf("OSS initialized clock: sec=%d nano=%d\n", clock[0], clock[1]);

    //day 4:need to pass two arguments to worker, seconds and nanoseconds, respectively. 
    int intervalSec = (int)t;
    int intervalNano = (int)((t - intervalSec) * NANOPERSEC); //billion
    if(intervalSec < 0) intervalSec = 0;;
    if(intervalNano < 0) intervalNano = 0;

    while (intervalNano >= NANOPERSEC) {
        intervalSec++; 
        intervalNano = intervalNano - NANOPERSEC; 
    }

    int runningChildren = 0;
    int finishedChildren = 0;
    int status = 0;


    for (int launchedChildren = 0; launchedChildren < n; launchedChildren++) {
        while(runningChildren >= s){
            *nano = *nano + INCREMENTNANO;;
            if(*nano >= NANOPERSEC){
                *sec = *sec + 1;
                *nano = *nano - NANOPERSEC;
        }

            pid_t childTerminated = waitpid(-1, &status, WNOHANG);
            if(childTerminated > 0){
                runningChildren--;
                finishedChildren++;
            } else if (childTerminated == 0){
                //no child has terminated, we can continue to increment time and check again
            } else if (childTerminated == -1){
                fprintf(stderr,"OSS: Error in waitpid\n");
                shmdt(clock);
                shmctl(shm_id, IPC_RMID, NULL);
                exit(1);
                }
            }

            //launch new child 

            pid_t pid = fork();
            if(pid == -1){
                fprintf(stderr,"OSS:... Error in fork\n");
                shmdt(clock);
                shmctl(shm_id, IPC_RMID, NULL);
                exit(1);
            }

            if (pid == 0){
                char secondStr[20], 
                        nanoStr[20];
                snprintf(secondStr, sizeof(secondStr), "%d", intervalSec);
                snprintf(nanoStr, sizeof(nanoStr), "%d", intervalNano);    
                execl("./worker", "worker", secondStr, nanoStr, (char *)NULL);
                fprintf(stderr,"OSS: Error in exec after fork\n");
                exit(1);
            }
            runningChildren++;;
        }

        while (finishedChildren < n){
            *nano = *nano + INCREMENTNANO;
            if(*nano >= NANOPERSEC){
                *sec = *sec + 1;
                *nano = *nano - NANOPERSEC;
        }

        pid_t childrenTerminated = waitpid(-1, &status, WNOHANG);
        if(childrenTerminated > 0){
            runningChildren--;
            finishedChildren++;
        } else if (childrenTerminated == 0){
        } else if (childrenTerminated == -1){
            fprintf(stderr,"OSS: Error in waitpid\n");
            shmdt(clock);
            shmctl(shm_id, IPC_RMID, NULL);
            exit(1);
        }
    }
        
        

        printf("OSS after children: sec=%d nano=%d\n", *sec, *nano);
        printf("OSS: Summary: Total children: %d, Finished children: %d\n", n, finishedChildren);

    shmdt(clock);
    shmctl(shm_id, IPC_RMID, NULL);
    return 0;
}