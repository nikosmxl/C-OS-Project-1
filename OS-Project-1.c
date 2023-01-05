#include "memory.h"

void children(int lines, int req, int num);

int main(int argc, char *argv[]){

    //The file we are reading
    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL){
        perror("File does not exist.\n");
        exit(1);
    }
    const unsigned int lines = atoi(argv[2]);
    const unsigned int req = atoi(argv[3]);

    // Number of lines for the 2d array and the longest line's length
    int lines_of_file = 0, currline_size = 0, longestline_size = 0;
    while(!feof(fp)){
        currline_size++;
        if(fgetc(fp) == '\n'){
            lines_of_file++;
            if (currline_size > longestline_size){
                longestline_size = currline_size;
            }
            currline_size = 0;
        }
    }
    if (currline_size > 1){
        lines_of_file++;
    }
    
    int size = lines_of_file/lines;
    if (lines_of_file%lines != 0){
        size++;
    }
    rewind(fp);
    
    // Allocations
    String **segs = malloc(size*sizeof(String*));
    for (int i = 0 ; i < size ; i++){
        segs[i] = malloc(lines*sizeof(String));
        for (int j = 0 ; j < lines ; j++){
            segs[i][j] = (String)malloc((longestline_size + 1)*sizeof(char));   // + 1 for '\0'
            fgets(segs[i][j], (longestline_size + 1), fp);
        }
    }
    fclose(fp);
    
    // Shared memory
	void *shared_memory = (void *)0;
	int shmid = shmget(SHMKEY, sizeof(struct shared_use_st), 0666 | IPC_CREAT);
	if (shmid == -1) {
		fprintf(stderr, "shmget failed\n");
		exit(EXIT_FAILURE);
	}
	shared_memory = shmat(shmid, (void *)0, 0);
	if (shared_memory == (void *)-1) {
		fprintf(stderr, "shmat failed\n");
		exit(EXIT_FAILURE);
	}
	struct shared_use_st *shared_stuff = (struct shared_use_st *)shared_memory;

    // The 2d arrays of the shared memory
    ipcs ipc_linechar = malloc(sizeof(struct ipc_storage));
    ipc_linechar->ipcs = malloc(longestline_size*sizeof(int));
    ipcs ipc_segreqs = malloc(sizeof(struct ipc_storage));
    ipc_segreqs->ipcs = malloc(size*sizeof(int));

    shm_for_lineschar(shared_stuff, lines, longestline_size, ipc_linechar);
    shm_for_segreqs(shared_stuff, size, ipc_segreqs);
    
    // Semseg set of semaphores
    // Every ith semaphore corresponds to the ith segment of the file and signals that this segment is in the shared memory the current moment
    int semseg = semget(SEMKEY, size, IPC_CREAT | 0666);
    if (semseg < 0) {
		perror("semget");
		exit(1);
	}
    
    for (int i = 0 ; i < size ; i++){
        if (semctl(semseg, i, SETVAL, 0) < 0) {
            perror("Could not set value of semaphore");
            exit(4);
        }
    }

    // Semaphore readers_mutex Initialization
    sem_t *readers_mutex = sem_open(READERSNAME, O_CREAT, SEM_PERMS, 1);
    if (readers_mutex == SEM_FAILED) {
        perror("sem_open(3) error");
        exit(EXIT_FAILURE);
    }

    // Not needed in the parent process
    if (sem_close(readers_mutex) < 0) {
        perror("sem_close(3) failed");
        sem_unlink(READERSNAME);
        exit(EXIT_FAILURE);
    }

    // Semaphore segreq_mutex Initialization
    sem_t *segreq_mutex = sem_open(SEGREQNAME, O_CREAT, SEM_PERMS, 1);
    if (segreq_mutex == SEM_FAILED) {
        perror("sem_open(3) error");
        exit(EXIT_FAILURE);
    }

    // Semaphore finished_mutex Initialization
    sem_t *finished_mutex = sem_open(FINISHEDNAME, O_CREAT, SEM_PERMS, 1);
    if (finished_mutex == SEM_FAILED) {
        perror("sem_open(3) error");
        exit(EXIT_FAILURE);
    }

    // Semaphore request_child Initialization
    sem_t *request_child = sem_open(REQUESTCHILDNAME, O_CREAT, SEM_PERMS, 0);
    if (request_child == SEM_FAILED) {
        perror("sem_open(3) error");
        exit(EXIT_FAILURE);
    }

    // Semaphore answer_child Initialization
    sem_t *answer_child = sem_open(ANSWERCHILDNAME, O_CREAT, SEM_PERMS, 0);
    if (answer_child == SEM_FAILED) {
        perror("sem_open(3) error");
        exit(EXIT_FAILURE);
    }

    // Creats a file for the parent's answers
	FILE* fptr = fopen(FILENAME, "w");
    if (fptr == NULL) {
        printf("Error!");
        exit(1);
    }
    
    //Forks
    pid_t pid[N];
    shared_stuff->finished = 0;
    shared_stuff->next_insert = 0;
    shared_stuff->size = size;
    shared_stuff->readers = 0;
    for (int i = 0 ; i < N ; i++){
        if ((pid[i] = fork()) < 0) {
            perror("fork(2) failed");
            exit(EXIT_FAILURE);
        }

        if (pid[i] == 0) {
            children(lines, req, i);
        }
    }
    
    struct sembuf sb;
    int next_seg, k = 0;
    long long t;
    while (1){
        sem_wait(request_child);    // Is there a request?
        sem_wait(finished_mutex);   // Did all the children finish?
        if (shared_stuff->finished == N){
            break;                  // If yes then process is done.
        }
        sem_post(finished_mutex);

        if (k > 0){
            fprintf(fptr, "Time of segment %d in shared memory: %lld milliseconds\n", next_seg + 1, timeInMilliseconds() - t);
        }
        t = timeInMilliseconds();

        next_seg = shared_stuff->segreqs[0][0];     // Segreqs[0][0] has always the next segment to load to shared memory
        for (int i = 0 ; i < lines ; i++){
            strcpy(shared_stuff->lineschar[i], segs[next_seg][i]);
        }
        sb.sem_flg = 0;
        sb.sem_num = next_seg;

        sem_wait(segreq_mutex);
        sb.sem_op = shared_stuff->segreqs[0][1];
        fix_array(shared_stuff);
        sem_post(segreq_mutex);
        
        if (semop(semseg, &sb, 1) < 0){             // Posting semaphore by Segreqs[0][1], meaning the number of requests that have been done
            perror("Could not increase or decrease semaphore");
            exit(5);
        }

        sem_wait(answer_child);                     // Shall i continue with the next request?
        k++;
    }
    
    fclose(fptr);

    //Wait for children to finish
    for (int i = 0; i < sizeof(pid)/sizeof(pid[0]); i++){
        if (waitpid(pid[i], NULL, 0) < 0) perror("waitpid(2) failed");
    }
    
    //Unlink Semaphores
    if (semctl(semseg, 0, IPC_RMID) == -1) {
        perror("semctl");
        exit(1);
    }

    if (sem_unlink(READERSNAME) < 0) perror("sem_unlink(3) failed");
    if (sem_unlink(REQUESTCHILDNAME) < 0) perror("sem_unlink(3) failed");
    if (sem_unlink(ANSWERCHILDNAME) < 0) perror("sem_unlink(3) failed");
    if (sem_unlink(SEGREQNAME) < 0) perror("sem_unlink(3) failed");
    if (sem_unlink(FINISHEDNAME) < 0) perror("sem_unlink(3) failed");
    
    // Delete 2d arrays in the shared memory
    for (int i = 0 ; i < longestline_size ; i++){
        if (shmctl(ipc_linechar->ipcs[i], IPC_RMID, NULL) == -1) {
            fprintf(stderr, "shmctl(IPC_RMID) failed\n");
            exit(EXIT_FAILURE);
        }
    }
    
    if (shmctl(ipc_linechar->ipc, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "shmctl(IPC_RMID) failed\n");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0 ; i < size ; i++){
        if (shmctl(ipc_segreqs->ipcs[i], IPC_RMID, NULL) == -1) {
            fprintf(stderr, "shmctl(IPC_RMID) failed\n");
            exit(EXIT_FAILURE);
        }
    }

    if (shmctl(ipc_segreqs->ipc, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "shmctl(IPC_RMID) failed\n");
        exit(EXIT_FAILURE);
    }

    // Delete shared memory
    if (shmctl(shmid, IPC_RMID, 0) == -1) {
		fprintf(stderr, "shmctl(IPC_RMID) failed %d\n", errno);
		exit(EXIT_FAILURE);
	}

    // Frees
    free(ipc_linechar->ipcs);
    free(ipc_linechar);
    free(ipc_segreqs->ipcs);
    free(ipc_segreqs);
    
    for (int i = 0 ; i < size ; i++){
        for (int j = 0 ; j < lines ; j++){
            free(segs[i][j]);
        }
        free(segs[i]);
    }
    free(segs);

    exit(EXIT_SUCCESS);
}

// 2d arrays in shared memory while keeping their ipcs to delete them later
void shm_for_lineschar(struct shared_use_st* shared_stuff, int lines, int longestline_size, ipcs ipc1){
    ipc1->ipc = shmget(IPC_PRIVATE, lines*sizeof(char**), 0666 | IPC_CREAT);
    if (ipc1->ipc == -1) {
        fprintf(stderr, "shmget failed\n");
        exit(EXIT_FAILURE);
    }
    shared_stuff->lineschar = (String*)shmat(ipc1->ipc, (void *)0, 0);
    if (shared_stuff->lineschar == (void *)-1) {
        fprintf(stderr, "shmat failed\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0 ; i < lines ; i++){
        ipc1->ipcs[i] = shmget(IPC_PRIVATE, (longestline_size + 1)*sizeof(char), 0666 | IPC_CREAT);
        if (ipc1->ipcs[i] == -1) {
            fprintf(stderr, "shmget failed\n");
            exit(EXIT_FAILURE);
        }
        shared_stuff->lineschar[i] = (String)shmat(ipc1->ipcs[i], (void *)0, 0);
        if (shared_stuff->lineschar[i] == (void *)-1) {
            fprintf(stderr, "shmat failed\n");
            exit(EXIT_FAILURE);
        }
    }

    return;
}

void shm_for_segreqs(struct shared_use_st* shared_stuff, int size, ipcs ipc2){
    ipc2->ipc = shmget(IPC_PRIVATE, size*sizeof(int*), 0666 | IPC_CREAT);
    if (ipc2->ipc == -1) {
        fprintf(stderr, "shmget failed\n");
        exit(EXIT_FAILURE);
    }
    shared_stuff->segreqs = (int**)shmat(ipc2->ipc, (void *)0, 0);
    if (shared_stuff->segreqs == (void *)-1) {
        fprintf(stderr, "shmat failed\n");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0 ; i < size ; i++){
        ipc2->ipcs[i]  = shmget(IPC_PRIVATE, 2*sizeof(int), 0666 | IPC_CREAT);
        if (ipc2->ipcs[i] == -1) {
            fprintf(stderr, "shmget failed\n");
            exit(EXIT_FAILURE);
        }
        shared_stuff->segreqs[i] = (int*)shmat(ipc2->ipcs[i], (void *)0, 0);
        if (shared_stuff->segreqs[i] == (void *)-1) {
            fprintf(stderr, "shmat failed\n");
            exit(EXIT_FAILURE);
        }
    }
    return;
}

// Fixing array so that the next segment to load in shared memory comes to segreqs[0][0]
void fix_array(struct shared_use_st* memory){
    for(int i = 0 ; i < memory->next_insert - 1 ; i++){
        for(int j = 0 ; j < 2 ; j++){
            memory->segreqs[i][j] = memory->segreqs[i+1][j];
        }
    }
    memory->next_insert--;

    return;
}

long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

long long timeInMicroseconds(void) {
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+(tv.tv_usec);
}