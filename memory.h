#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <sys/time.h>

#define SEM_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)
#define SHMKEY (key_t)1234
#define SEMKEY (key_t)5555
#define N 3

#define READERSNAME "readers_mutex"
#define SEGREQNAME "segreq_mutex"
#define FINISHEDNAME "finished_mutex"
#define REQUESTCHILDNAME "request_mutex"
#define ANSWERCHILDNAME "answer_mutex"

#define FILENAME "Parent_File"
#define CHILDNAME "Child_File"

typedef char * String;

struct shared_use_st {
	String* lineschar;  // The segment
    int** segreqs;  // 2D array with 2 columns, first one for the next segment to get in the shared memory, second one for the number of requests for that segment
    int size;   // Size of segreqs
    short finished;   // Children that have finished
    short next_insert;    // Shows where the children are going to insert next time
    short readers;    // Readers inside the shared memory
};

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};

typedef struct ipc_storage {
    int ipc;
	int* ipcs;
}* ipcs;

void shm_for_lineschar(struct shared_use_st*, int, int, ipcs);

void shm_for_segreqs(struct shared_use_st*, int, ipcs);

void fix_array(struct shared_use_st*);

int find(struct shared_use_st*, int);

long long timeInMilliseconds(void);

long long timeInMicroseconds(void);