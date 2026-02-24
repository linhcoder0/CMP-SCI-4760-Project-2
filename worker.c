#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

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
    int *nano = &clock[1];

    printf("Child sees:\t sec %d , nanosecond %d\n", *sec, *nano);

    printf("Changing clock to 5 , 13\n");
    *sec = 5;
    *nano = 13;
    // The child will not be modifying the clock later in this project. ^^^ 

    printf("Child now:\t sec %d , nanosecond %d\n", *sec, *nano);

    shmdt(clock);
    return 0;
}