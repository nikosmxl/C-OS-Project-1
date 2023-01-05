#include "memory.h"

int children(int lines, int req, int num) {

    // Shared memory
	void *shared_memory = (void *)0;
	int shmid = shmget(SHMKEY, sizeof(struct shared_use_st), 0666 | IPC_CREAT);
	if (shmid == -1) {
		fprintf(stderr, "shmget failed %d\n", errno);
		exit(EXIT_FAILURE);
	}
	shared_memory = shmat(shmid, (void *)0, 0);
	if (shared_memory == (void *)-1) {
		fprintf(stderr, "shmat failed\n");
		exit(EXIT_FAILURE);
	}
	printf("Shared memory segment with id %d attached at %p\n", shmid, shared_memory);

	struct shared_use_st *shared_stuff = (struct shared_use_st *)shared_memory;
	
	//Mutex
    sem_t *readers_mutex = sem_open(READERSNAME, O_RDWR);
    if (readers_mutex == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }

	//Segreq_mutex
    sem_t *segreq_mutex = sem_open(SEGREQNAME, O_RDWR);
    if (segreq_mutex == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }

	//Finished_mutex
    sem_t *finished_mutex = sem_open(FINISHEDNAME, O_RDWR);
    if (finished_mutex == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }

	//Request_child
    sem_t *request_child = sem_open(REQUESTCHILDNAME, O_RDWR);
    if (request_child == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }

	//Answer_child
    sem_t *answer_child = sem_open(ANSWERCHILDNAME, O_RDWR);
    if (answer_child == SEM_FAILED) {
        perror("sem_open(3) failed");
        exit(EXIT_FAILURE);
    }

	//Semseg
	int semseg = semget(SEMKEY, shared_stuff->size, 0666);
    if (semseg < 0) {
		perror("semget");
		exit(1);
	}

	// The name of the child's file
	char str[11] = CHILDNAME;
	char str2[5];	// Up to 9999 children
	sprintf(str2, "%d", num);
	strcat(str, str2);

	// Creating file
	FILE* fptr = fopen(str, "w");
    if (fptr == NULL) {
        printf("Error!");
        exit(1);
    }

    srand((unsigned int)getpid());

	int child_requests = 0;

	int seg_wanted = rand() % shared_stuff->size;
	do{
		if (child_requests > 0 && (rand() % 10 + 1) <= 3){		// Possibilities 0.7 for the same segment to request
			seg_wanted = rand() % shared_stuff->size;
		}
		int line_wanted = rand() % lines;

		long long t1 = timeInMicroseconds();
		
		sem_wait(segreq_mutex);
		int found_seg = find(shared_stuff, seg_wanted);
		if(found_seg == -1){									// If the segment has not been requested yet, insert it to segreqs
			shared_stuff->segreqs[shared_stuff->next_insert][0] = seg_wanted;
			shared_stuff->segreqs[shared_stuff->next_insert][1] = 1;
			shared_stuff->next_insert++;
			sem_post(request_child);							// New request has been made, parent will go read it
		}
		else{													// If the segment has already been requested just increase the number of requests
			shared_stuff->segreqs[found_seg][1]++;
		}
		sem_post(segreq_mutex);

		struct sembuf sb;
		sb.sem_flg = 0;
		sb.sem_num = seg_wanted;
		sb.sem_op = -1;

		t1 = timeInMicroseconds() - t1;
		long long t2 = timeInMilliseconds();
		
		if (semop(semseg, &sb, 1) < 0){							// Wait until the requested seg is in the shared memory
			perror("Could not increase or decrease semaphore");
			exit(5);
		}
		
		t2 = timeInMilliseconds() - t2;

		sem_wait(readers_mutex);
		shared_stuff->readers++;
		sem_post(readers_mutex);

		// Writing answers in the child's file
		fprintf(fptr, "Request time: %lld microseconds\n", t1);
		fprintf(fptr, "Respond time: %lld milliseconds\n", t2);
		fprintf(fptr, "Request: <%d,%d>\n", seg_wanted + 1, line_wanted + 1);
		fprintf(fptr, "Line: %s\n", shared_stuff->lineschar[line_wanted]);

		usleep(20000);	// Sleep 20 ms

		sem_wait(readers_mutex);
		shared_stuff->readers--;
		if (shared_stuff->readers == 0){
			sem_post(answer_child);								// When all children read the requested segment, continue to next request
		}
		sem_post(readers_mutex);

		child_requests++;
	}while (child_requests < req);
	sem_wait(finished_mutex);
	shared_stuff->finished++;
	if (shared_stuff->finished == N){
		sem_post(answer_child);									// Just for the program to go read the finished signal
		sem_post(request_child);
	}
	sem_post(finished_mutex);
	
	fclose(fptr);

	// Semaphores closing
    if (sem_close(readers_mutex) < 0) perror("sem_close(3) failed");
	if (sem_close(segreq_mutex) < 0) perror("sem_close(3) failed");
	if (sem_close(finished_mutex) < 0) perror("sem_close(3) failed");
	if (sem_close(answer_child) < 0) perror("sem_close(3) failed");
    
	// Memory detachment
	if (shmdt(shared_memory) == -1) {
		fprintf(stderr, "shmdt failed\n");
		exit(EXIT_FAILURE);
	}

    exit(EXIT_SUCCESS);
}

int find(struct shared_use_st* shared_stuff, int seg_requested){
	for (int i = 0 ; i < shared_stuff->next_insert ; i++){
		if (shared_stuff->segreqs[i][0] == seg_requested){
			return i;
		}
	}
	return -1;
}