#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <getopt.h> 
#include <unistd.h>
#include <signal.h>
#include <string.h> 
#include <errno.h>

static volatile sig_atomic_t shutdownFlag = 0;
static volatile sig_atomic_t shutdownSig = 0;
const size_t BUFF_SZ = sizeof(int) * 2; 

struct PCB {
    int occupied; // either true or false
    pid_t pid; // process id of this child
    int startSeconds; // time when it was forked
    int startNano; // time when it was forked
    int endingTimeSeconds; // estimated time it should end
    int endingTimeNano; // estimated time it should end
};

static int shm_id_global = -1;
static int *clock_global = NULL;

void signal_handler(int sig) {
    shutdownFlag = 1;
    shutdownSig = sig;
}

#define MAX_PCB_SIZE 20
#define PRINT_INTERVAL_NS 500000000LL       // 0.5 sec
struct PCB processTable[MAX_PCB_SIZE];

int main(int argc, char *argv[]) {
    signal(SIGALRM, signal_handler);
    signal(SIGINT,  signal_handler);
    alarm(60);
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

    if (n <= 0) n = 1;
    if (s <= 0) s = 1;
    if (t <= 0) t = 1.0f;
    if (i < 0) i = 0.0f;
    
    if (s > n) s = n;
    if (s > MAX_PCB_SIZE) s = MAX_PCB_SIZE; 

    const int NANOPERSEC = 1000000000; //1 second
    const int INCREMENTNANO = 10000000; //10ms

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
    shm_id_global = shm_id;
    clock_global = clock;
    // init clock values to 0,0
    int *sec  = &(clock[0]);
    int *nano = &(clock[1]);
    *sec = 0;
    *nano = 0;

    printf("OSS starting, PID:%d PPID:%d\n", getpid(), getppid());
    printf("Called with:\n-n %d\n-s %d\n-t %.3f\n-i %.3f\n", n, s, t, i);
    printf("OSS initialized clock: sec=%d nano=%d\n", clock[0], clock[1]);

    //initialize process table
    for (int m = 0; m < MAX_PCB_SIZE; m++) {
        processTable[m].occupied = 0;
        processTable[m].pid = 0;
        processTable[m].startSeconds = 0;
        processTable[m].startNano = 0;
        processTable[m].endingTimeSeconds = 0;
        processTable[m].endingTimeNano = 0;
    }

    //max worker lifetime
    int workerMaxTimeSec = (int)t;
    int workerMaxTimeNano = (int)((t - workerMaxTimeSec) * NANOPERSEC);
    if(workerMaxTimeSec < 0) workerMaxTimeSec = 0;
    if(workerMaxTimeNano < 0) workerMaxTimeNano = 0;
    while (workerMaxTimeNano >= NANOPERSEC) {
        workerMaxTimeSec++; 
        workerMaxTimeNano = workerMaxTimeNano - NANOPERSEC; 
    }   

    //time between worker launches
    int launchIntervalSec = (int)i;
    int launchIntervalNano = (int)((i - launchIntervalSec) * NANOPERSEC);
    if(launchIntervalSec < 0) launchIntervalSec = 0;
    if(launchIntervalNano < 0) launchIntervalNano = 0;
    while (launchIntervalNano >= NANOPERSEC) {
        launchIntervalSec++; 
        launchIntervalNano = launchIntervalNano - NANOPERSEC; 
    }

    int runningChildren = 0;
    int finishedChildren = 0;
    int launchedChildren = 0;
    int status = 0;

    long long nextPrintNS = PRINT_INTERVAL_NS; //next time to print process table, in nanoseconds, starts at 0.5s
    //keep track of last launch time to know when to launch next worker based on launch interval
    int lastLaunchSec = 0;
    int lastLaunchNano = 0;
    int haveLaunchedAny = 0;
    // for (int launchedChildren = 0; launchedChildren < n; launchedChildren++) {

        //main loop will run until we have launched n children and all of them have finished
        while (!shutdownFlag && (launchedChildren < n || runningChildren > 0)) {

            *nano = *nano + INCREMENTNANO;
            if(*nano >= NANOPERSEC){
                *sec = *sec + 1;
                *nano = *nano - NANOPERSEC;
            }
            
            long long nowNS = (long long)(*sec) * (long long)NANOPERSEC + (long long)(*nano);

            if (nowNS >= nextPrintNS) {
                printf("\nOSS PID:%d SysClockS: %d SysclockNano: %d\n",
                    (int)getpid(), *sec, *nano);
                printf("Process Table:\n");
                printf("Entry Occupied PID StartS StartN EndingTimeS EndingTimeNano\n");

                for (int k = 0; k < MAX_PCB_SIZE; k++) {
                    printf("%2d %8d %6d %6d %6d %11d %13d\n",
                        k,
                        processTable[k].occupied,
                        (int)processTable[k].pid,
                        processTable[k].startSeconds,
                        processTable[k].startNano,
                        processTable[k].endingTimeSeconds,
                        processTable[k].endingTimeNano);
                }
                fflush(stdout);

                while (nextPrintNS <= nowNS) nextPrintNS += PRINT_INTERVAL_NS;
            }


            while(1){
            pid_t terminatedPid = waitpid(-1, &status, WNOHANG);
            if(terminatedPid > 0){
                //clear the process table entry for this child that just terminated
                for (int k = 0; k < MAX_PCB_SIZE; k++) {
                    if (processTable[k].occupied == 1 && processTable[k].pid == terminatedPid) {
                        processTable[k].occupied = 0;
                        processTable[k].pid = 0;
                        processTable[k].startSeconds = 0;
                        processTable[k].startNano = 0;
                        processTable[k].endingTimeSeconds = 0;
                        processTable[k].endingTimeNano = 0;
                        break;
                    }
                }
                //update count of running and finished children
                runningChildren--;
                finishedChildren++;
                continue; 
            } 
            if (terminatedPid == 0){
                break; //no more children have terminated
                //break out of loop and continue with rest of main loop
            }
            if (errno == ECHILD) {
                break; //no more children to wait for
            }
                else {
                fprintf(stderr,"OSS:... Error in waitpid\n");
                shmdt(clock);
                shmctl(shm_id, IPC_RMID, NULL);
                exit(1);
            }
        }

            //check if it's time to launch a new worker based on launch interval and if we still have workers to launch
            if(launchedChildren < n && runningChildren < s) {
                //give the okay to launch a new worker if it's 
                //the first launch or if enough time has passed since last launch 
                int greenLight = 0;
                if (launchIntervalSec == 0 && launchIntervalNano == 0) {
                    greenLight = 1;
                } else if (!haveLaunchedAny){
                    greenLight = 1;
                } else {
                    //calculate time elapsed since last launch and compare to launch int
                    long long now = (long long)(*sec) * NANOPERSEC + *nano;
                    long long last = (long long)lastLaunchSec * NANOPERSEC + lastLaunchNano;
                    long long interval = (long long)launchIntervalSec * NANOPERSEC + launchIntervalNano;
                    if (now - last >= interval) {
                        greenLight = 1;
                    }
                }

                if (greenLight) {
                    int slot = -1;
                    for (int k = 0; k < MAX_PCB_SIZE; k++) {
                        if (processTable[k].occupied == 0) {
                            slot = k;
                            break;
                }
            }

            if (slot != -1){
                //found an open slot in the process table, so launch worker and fill in PCB info
                processTable[slot].occupied = 1;
                processTable[slot].startSeconds = *sec;
                processTable[slot].startNano = *nano;

                processTable[slot].endingTimeSeconds = *sec + workerMaxTimeSec;
                processTable[slot].endingTimeNano = *nano + workerMaxTimeNano;
                if(processTable[slot].endingTimeNano >= NANOPERSEC) {
                    processTable[slot].endingTimeSeconds++;
                    processTable[slot].endingTimeNano = processTable[slot].endingTimeNano - NANOPERSEC;
                }

                pid_t pid = fork();
                if(pid == -1){
                    fprintf(stderr, "OSS: Error in fork\n");
                    processTable[slot].occupied = 0;
                    shmdt(clock);
                    shmctl(shm_id, IPC_RMID, NULL);
                    exit(1);
            }

                if(pid == 0) {
                    char secondStr[20], nanoStr[20];
                    snprintf(secondStr, sizeof(secondStr), "%d", workerMaxTimeSec);
                    snprintf(nanoStr, sizeof(nanoStr), "%d", workerMaxTimeNano);    
                    execl("./worker", "worker", secondStr, nanoStr, (char *)NULL);
                    fprintf(stderr,"OSS: Error in exec after fork\n");
                    exit(1);
                }

                //parent
                processTable[slot].pid = pid;
                runningChildren++;
                launchedChildren++;

                //update last launch time
                lastLaunchSec = *sec;
                lastLaunchNano = *nano;
                haveLaunchedAny = 1;
            }
        }
    }
}
if (shutdownFlag) {
    fprintf(stderr, "\nOSS: shutdown triggered by %s\n",
            (shutdownSig == SIGALRM) ? "SIGALRM (60s timeout)" : "SIGINT (Ctrl-C)");

    // kill all children we know about
    for (int k = 0; k < MAX_PCB_SIZE; k++) {
        if (processTable[k].occupied && processTable[k].pid > 0) {
            kill(processTable[k].pid, SIGTERM);
        }
    }

    // reap them (blocking)
    while (waitpid(-1, &status, 0) > 0) {}

    // cleanup shared memory
    if (clock_global != NULL) shmdt(clock_global);
    if (shm_id_global != -1) shmctl(shm_id_global, IPC_RMID, NULL);

    exit(1);
}
}


