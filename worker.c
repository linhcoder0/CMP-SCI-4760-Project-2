#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

const size_t BUFF_SZ = sizeof(int) * 2; 
// had to change int to size_t to avoid compiler warning 
//about comparison between signed and unsigned integers

int main(int argc, char *argv[]) {
  //The worker takes in two command line arguments, this time corresponding to the maximum time it should decide to stay around
  // in the system. For example, if you were running it directly you might call it like:
  // ./worker 5 500000
  if(argc < 3) {
    fprintf(stderr, "WORKER: ./worker [second] [nanosecond]\n");
    exit(1);
  }

  //convert strings to int for sec and nanosec
  int intervalSec = atoi(argv[1]);
  int intervalNano = atoi(argv[2]);
  if(intervalSec < 0) intervalSec = 0;;
  if(intervalNano < 0) intervalNano = 0;

  while(intervalNano >= 1000000000) {
    //if nanosec is greater than or equal to 1 billion, we need to convert it to seconds and nanoseconds
    intervalSec++; 
    intervalNano = intervalNano - 1000000000; 
  }
    //It should first output what it was called with as well as its PID and PPID.
    // Worker starting, PID:6577 PPID:6576
    // Called with:
    // Interval: 5 seconds, 500000 nanoseconds

    printf("Worker starting, PID:%d PPID:%d\n", getpid(), getppid());
    printf("Called with:\nInterval: %d seconds, %d nanoseconds\n", intervalSec, intervalNano);

  key_t shm_key = ftok("oss.c", 0); 
//int shm_key changed to key_t 
//to match the return type of ftok, 
//and avoid compiler warning about 
//comparison between signed and unsigned integers
    if (shm_key == (key_t)-1) {
		fprintf(stderr,"Child:... Error in ftok\n");        
        exit(1);
    }

    int shm_id = shmget(shm_key, BUFF_SZ, 0700);
    if (shm_id == -1) {
		fprintf(stderr,"child:... Error in shmget\n");
		exit(1);
    }

    int *clock = (int *)shmat(shm_id, NULL, 0);
    if (clock == (void *)-1) {
		fprintf(stderr,"Child:... Error in shmat\n");
		exit(1);
    }

    int *sec  = &clock[0];
    int startSec = *sec; //keep track of start time in seconds for printing elapsed time later
    int *nano = &clock[1];

    //Compute termination time by adding the interval to the current time in shared memory.
    int termSec = *sec + intervalSec;
    int termNano = *nano + intervalNano;
    if(termNano >= 1000000000) {
      termSec++;
      termNano = termNano - 1000000000;
    }

    //So what output should the worker send? Upon starting up, it should output the PID, its PPID, the system clock and when it will
// terminate. This should be in the following format:
// WORKER PID:6577 PPID:6576
// SysClockS: 5 SysclockNano: 1000 TermTimeS: 11 TermTimeNano: 500100
// --Just Starting
    printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
    printf("SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", *sec, *nano, termSec, termNano);
    printf("--Just Starting\n");
    
    //Keep track of the last time we printed a msg to only print when second changes 
    int lastSec = *sec;
    // int lastNano = *nano;

  while(1){
    //The worker will then attach to shared memory and examine our simulated system clock. It will then figure out what time it
// should terminate by adding up the system clock time and the time passed to it (in our simulated system clock, not actual time).
// This is when the process should decide to leave the system and terminate
//     The worker will then go into
// a loop, constantly checking the system clock to see if this time has passed. If it ever looks at the system clock and sees values
// over the ones when it should terminate, it should output some information and then terminate
    int currentSec = *sec;
    int currentNano = *nano;
    //time pass termination time. print and exit.
    if(currentSec > termSec || (currentSec == termSec && currentNano >= termNano)) {
      printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
      printf("SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", currentSec, currentNano, termSec, termNano);
      printf("--Terminating\n");
      shmdt(clock); 
      exit(0);
    }
    //everytime seconds change, print msg with time elapsed since starting
    if(currentSec != lastSec) {
      printf("WORKER PID:%d PPID:%d\n", getpid(), getppid());
      printf("SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n", currentSec, currentNano, termSec, termNano);
      printf("--%d seconds have passed since starting\n", currentSec - startSec);
      lastSec = currentSec; 
      }
    }
    shmdt(clock);
    return 0;
}