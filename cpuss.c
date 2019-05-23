#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>

#define PROCESS_MAX 20
#define CPU_BIRST_MAX 50
#define IO_BIRST_LENGTH_MAX 20
#define ARRIVAL_TIME_MAX 100
#define PRIORITY_MAX 30
#define IO_INTERRUPT_MAX 3
#define RR_TIMESLICE 5
#define CFS_BASE_TIMESLICE 2

#define CPU_CORES 4

#define FCFS 1
#define NPSJF 2
#define NPPRIO 3
#define RR 4
#define PSJF 5
#define PPRIO 6
#define CFS 7

typedef int bool;
#define true 1
#define false 0

static int pid_ref = 1;


typedef struct eval_cpu{
	int total_runtime;
	int total_wait_time;
	float avg_wait_time;
	int total_tur_time;
	float avg_tur_time;
	int cpu_util_time[CPU_CORES];
	int cpu_idle_time[CPU_CORES];
}EVAL_CPU;

typedef struct process{

	/*initialized values*/
	int pid;
	int cpu_t;  //cpu birst time
	int io_t;  // i/o birst time
	int arrival_t; //arival time
	int priority;  //priority
	int io_count;  //number of i/o interrupts

	/*variables*/
	int io_interrupted;  //internal value for calculating number of i/o intterupts

	int cpu_used;  //internal value for calculating cpu used
	int io_waited;  //internal value for calculate waiting time
	int t_wait; //total wait time
	int tur_t; //total turnaround time

	int rr_ts_used; //roud-robin timeslice used

	float vruntime;
	int cfs_timeslice;
	float weight;

	int last_executed; //internal value for calculating waiting time

	int* io_loc;  // location of i/o interrupt in cpu_t
	int* io_length; //length of each i/o intteruption
}PROCESS;

typedef struct node{
	PROCESS* process;
	struct node* llink;
	struct node* rlink;
}NODE;

typedef struct queue{
	int ready_count;
	int waiting_count;
	NODE* ready_head;
	NODE* ready_rear;
	NODE* waiting_head;
	NODE* waiting_rear;
	PROCESS* running[CPU_CORES];
}QUEUE;

typedef struct cpu{
	int length;
	NODE* cpu_head;
	NODE* cpu_rear;
}CPU;   //for final result evaluation


FILE* fd ;
FILE* plog;


static int _search(NODE** pPre, NODE** pLoc, PROCESS process, int (*compare)(PROCESS p1, PROCESS p2)){
	while(*pLoc!=NULL && compare(process, *((*pLoc)->process)) > 0){
		*pPre = *pLoc;
		*pLoc = (*pLoc)->rlink;
	}
	if(*pLoc == NULL)
		return 0;
	else if((*pLoc)->process->pid == process.pid)
		return 1;
	else 
		return 0;
}

static int _insert( NODE** headPtr, NODE** rearPtr, NODE* pPre, PROCESS* data,int* count){
	NODE* pNew = (NODE*)malloc(sizeof(NODE));
	if(pNew == NULL)
		return 0;
	if(pPre == NULL){
		pNew->rlink = *headPtr;
		*headPtr = pNew;
	} else { 
		pNew->rlink = pPre->rlink;
		pPre->rlink = pNew;
	}
	if(pNew->rlink == NULL)
		*rearPtr = pNew;
	else
		pNew->rlink->llink = pNew;
	pNew->process = data;
	pNew->llink = pPre;
	*count += 1;
	return 1;
}

static void _delete( NODE** headPtr, NODE** rearPtr,  NODE* pPre, NODE* pLoc, int* count){
	if(pLoc == NULL)
		return;
	if(pLoc->rlink != NULL && pPre != NULL){
		pPre->rlink = pLoc->rlink;
		pLoc->rlink->llink = pPre;
	}
	if(pPre == NULL){
		*headPtr = pLoc->rlink;
		if(pLoc->rlink != NULL)
			pLoc->rlink->llink = NULL;
	}
	if(pLoc->rlink == NULL){
		*rearPtr = pPre;
		if(pPre != NULL)
			pPre->rlink = NULL;
	}

	free(pLoc);
	*count -= 1;
	return;
}

void destroyQueue(NODE* head){
	while(head != NULL){
		NODE* temp = head;
		head = head->rlink;
		free(temp);
	}
}

static void printNode(NODE* headPtr){
	while(headPtr!=NULL){
		printf("pid%d->", headPtr->process->pid);
		headPtr++;
	}
}

void setVruntime(PROCESS* process){
	process->vruntime = process->cpu_used / process->weight;
}

int compare_shorter_vruntime(PROCESS p1, PROCESS p2){
	if(p1.vruntime > p2.vruntime)
		return -1;
	else if(p1.vruntime < p2.vruntime)
		return 1;
	else if(p1.vruntime == p2.vruntime)
		return 0;
}

static int enque_vruntime(NODE** headPtr, NODE** rearPtr,void* np, PROCESS* process, int* count){
	NODE* pPre = NULL;
	NODE* pLoc = *headPtr;
	setVruntime(process);
	_search(&pPre, &pLoc, *process, compare_shorter_vruntime);
	_insert(headPtr, rearPtr, pPre, process, count);
	return 1;
}

int compare_shorter_job(PROCESS p1, PROCESS p2){
	int p1_time_left = p1.cpu_t - p1.cpu_used;
	int p2_time_left = p2.cpu_t - p2.cpu_used;
	return p2_time_left - p1_time_left;
}

static int enque_SJF(NODE** headPtr, NODE** rearPtr,void* np, PROCESS* process, int* count){
	NODE* pPre = NULL;
	NODE* pLoc = *headPtr;
	_search(&pPre, &pLoc, *process, compare_shorter_job);
	_insert(headPtr, rearPtr, pPre, process, count);
	return 1;
}

int compare_priority(PROCESS p1, PROCESS p2){
	return p1.priority - p2.priority;
}

static int enque_Priority(NODE** headPtr, NODE** rearPtr,void* np, PROCESS* process, int* count){
	NODE* pPre = NULL;
	NODE* pLoc = *headPtr;
	_search(&pPre, &pLoc, *process, compare_priority); 
	_insert(headPtr, rearPtr, pPre, process, count);
	return 1;
}



/* quicksort for int arr */
int partition_int(int* arr, int p, int r){
	int i = p + rand()%(r-p+1);
	int temp;
	temp = arr[r];
	arr[r] = arr[i];
	arr[i] = temp;

	i = p-1;
	int j;
	for(j=p;j<r;j++){
		if(arr[r]>=arr[j]){
			i +=1;
			temp = arr[j];
			arr[j] = arr[i];
			arr[i] = temp;
		}
	}
	temp = arr[i+1];
	arr[i+1] = arr[r];
	arr[r] = temp;
	return i+1;
}

void quickSort_int(int* arr, int p, int r){
	if(p<r){
		int q = partition_int(arr, p, r);
		quickSort_int(arr, p, q-1);
		quickSort_int(arr, q+1, r);
	}
}

/* QuickSort to sort randomly generated process  */
int partition(PROCESS* input,int p, int r, int (*compare)(PROCESS p1, PROCESS p2)){
	int i = p + rand()%(r-p+1);
	PROCESS temp;
	temp = input[r];
	input[r] = input[i];
	input[i] = temp;

	PROCESS x = input[r];
	i = p-1;
	int j;
	for(j=p;j<r;j++){
		if((*compare)(x, input[j])>=0){
			i+=1;
			temp = input[j];
			input[j] = input[i];
			input[i] = temp;
		}
	}
	temp = input[i+1];
	input[i+1] = input[r];
	input[r] = temp;

	return i+1;
} 

void quickSort(PROCESS* input, int p, int r, int (*compare)(PROCESS p1, PROCESS p2)){
	if(p<r){
		int q = partition(input, p, r, compare);
		quickSort(input, p, q-1, compare);
		quickSort(input, q+1, r, compare);
	}
}


int compare_arrival_time(PROCESS p1, PROCESS p2){
	return p1.arrival_t - p2.arrival_t;
}

// End of QuickSort

PROCESS create_Process(){
	PROCESS ret;
	ret.pid = pid_ref++;
	ret.cpu_t = 1 + rand() % CPU_BIRST_MAX;
	ret.arrival_t = rand() % (ARRIVAL_TIME_MAX+1);
	ret.priority = 1 + rand() % (PRIORITY_MAX);

	ret.cpu_used = 0;
	ret.io_interrupted = 0;
	ret.io_waited = 0;
	ret.t_wait = 0;
	ret.tur_t = 0;

	ret.rr_ts_used = 0;

	ret.vruntime = 0;
	ret.weight = (float)5*ret.priority / PRIORITY_MAX;
	ret.cfs_timeslice = 1 + ret.weight * CFS_BASE_TIMESLICE;

	ret.last_executed = ret.arrival_t;

	ret.io_t = 0;

	if(ret.cpu_t == 1)
		ret.io_count = 0;
	else
		ret.io_count = rand()%(IO_INTERRUPT_MAX+1);
	ret.io_loc = (int*)malloc(sizeof(int)*(ret.io_count+1));
	ret.io_length = (int*)malloc(sizeof(int)*ret.io_count);


	int i = 0;
	for(i=0;i<ret.io_count;i++){
		ret.io_loc[i] = 1 + rand()%(ret.cpu_t-1);
		ret.io_length[i] = 1+  rand()%IO_BIRST_LENGTH_MAX;
		ret.io_t += ret.io_length[i];
	}


	fprintf(fd, "\n");
	quickSort_int(ret.io_loc, 0, ret.io_count-1);
	ret.io_loc[ret.io_count] = 0;



	for(i = 0;i<ret.io_count;i++)
		fprintf(fd, "io_loc : %d , io_length : %d\n", ret.io_loc[i], ret.io_length[i]);
	//////////////////////////////////////////

	fprintf(fd, "created PID : %d, arrival_t : %d, cpu_t : %d, priority : %d\n", ret.pid, ret.arrival_t, ret.cpu_t,ret.priority);

	fprintf(plog, "===process PID: %d===\n",ret.pid);
	fprintf(plog, "arrival time: %d, cpu time: %d, priority: %d\n",ret.arrival_t, ret.cpu_t,ret.priority);
	fprintf(plog, "times of I/O: %d\n", ret.io_count);
	for(i = 0;i<ret.io_count;i++)
		fprintf(plog, "location: %d, length: %d\n",ret.io_loc[i],ret.io_length[i]);


	return ret;
}//create_Process()

void remove_Process(PROCESS* p){
	free(p->io_loc);
	free(p->io_length);
}

void resetProcess(PROCESS* p){
	p->io_interrupted = 0;
	p->cpu_used = 0;
	p->io_waited = 0;
	p->t_wait = 0;
	p->tur_t=0;
	p->rr_ts_used = 0;

	p->last_executed = p->arrival_t;
}

void printProcessData(FILE* output, PROCESS* p){
	fprintf(plog, "===process PID: %d===\n",p->pid);
	fprintf(plog, "arrival time: %d, cpu requirement time: %d priority: %d\n",p->arrival_t, p->cpu_t,p->priority);
	fprintf(plog, "times of I/O: %d\n", p->io_count);

	fprintf(plog, "process waiting time: %d\n", p->t_wait);
	fprintf(plog, "process turnaround time: %d\n\n", p->tur_t);
}

QUEUE configure(){

	QUEUE ret;
	ret.ready_head = NULL;
	ret.ready_rear = NULL;
	ret.waiting_head = NULL;
	ret.waiting_rear = NULL;
	ret.ready_count = 0;
	ret.waiting_count = 0;

	ret.running[CPU_CORES];

	int i;
	for(i=0;i<CPU_CORES;i++)
		ret.running[i] = NULL;

	return ret;
}

int scheduling_done(int ready_count, PROCESS** running, int pid, int waiting_count){
	if(ready_count > 0 || pid != -1 || waiting_count > 0 )
		return 1;
	else {
		for(int i=0;i<CPU_CORES;i++){
			if(running[i] != NULL)
				return 1;
		}
		return 0;
	}
}

CPU* schedule(int val, PROCESS* arr){   //val, for further input variation, arr : array of processes 

	fprintf(fd, "\n\n start scheduling by val: %d\n\n", val);

	//initializing ret;
	CPU* ret = (CPU*)malloc(sizeof(CPU) * CPU_CORES);
	for(int i =0;i<CPU_CORES;i++){
		ret[i].cpu_head = NULL;
		ret[i].cpu_rear = NULL;
		ret[i].length = 0;
	}

	int count = 0;
	while(arr[count].pid != -1)
		count++;
	count -= 1;

	quickSort(arr,0,count,compare_arrival_time);  //sorting arr by arrival time

	/* for check*/
	fprintf(fd, "end quicksort\n");
	int temp = 0;
	while(arr[temp].pid != -1){
		fprintf(fd, "pid : %d  arrival time : %d \n",arr[temp].pid, arr[temp].arrival_t);
		temp++;
	}
	//////////////////////////////////////////////////

	int j = 0;

	QUEUE queue = configure(); 


	switch(val){
		case FCFS: 
			{

				/*FCFS scheduling
				 */

				int i = 0;
				int time = 0;

				while(scheduling_done(queue.ready_count, queue.running, arr[i].pid, queue.waiting_count)){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->cpu_used == queue.running[j]->io_loc[queue.running[j]->io_interrupted]){

							fprintf(fd, "running to waiting\n");

							queue.running[j]->io_waited = queue.running[j]->io_length[queue.running[j]->io_interrupted];
							queue.running[j]->io_interrupted += 1;
							_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running[j],&queue.waiting_count);
							queue.running[j] = NULL;
						} // running to waiting(I/O interrupt)
					}

					if(queue.waiting_count != 0){

						fprintf(fd, "check waiting list\n");

						NODE* pLoc = queue.waiting_head;
						while(pLoc!=NULL && pLoc->rlink != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]){
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
								pLoc- pLoc->rlink;
							} else if(pLoc->process->io_waited == 0){

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								_insert(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								pLoc = pLoc->rlink;
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink->llink,pLoc->llink, &queue.waiting_count);
							} else { 
								pLoc->process->io_waited -= 1;
								pLoc = pLoc->rlink;
							}

						}// while

						if(pLoc != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]) {
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
							} else if (pLoc->process->io_waited == 0) {

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								_insert(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink,pLoc, &queue.waiting_count);
							} else {
								pLoc->process->io_waited -= 1;
							}
						}
					}//waiting to ready

					if(arr[i].pid != -1 && time == arr[i].arrival_t){

						while(arr[i].pid != -1 && time == arr[i].arrival_t){

							fprintf(fd, "arr to ready \n");

							_insert(&queue.ready_head, &queue.ready_rear, NULL, &arr[i], &queue.ready_count);
							i++;
						}
					}//arr to ready queue

					for(j=0;j<CPU_CORES;j++){

						if(queue.running[j] == NULL && queue.ready_rear != NULL){

							fprintf(fd, "ready to running\n");

							queue.running[j] = queue.ready_rear->process;
							queue.running[j]->t_wait += (time - queue.running[j]->last_executed);
							_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
						}//ready queue to running
					}


					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL){

							fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running[j]->pid,queue.running[j]->cpu_t, queue.running[j]->cpu_used);

							queue.running[j]->cpu_used += 1;

							//	printf(" after cpu_used increase\n");

							queue.running[j]->last_executed = time;

							//	printf("after recording last_executed\n");
						}
					}

					for(j=0;j<CPU_CORES;j++){
						_insert(&ret[j].cpu_head, &ret[j].cpu_rear, NULL, queue.running[j], &(ret[j].length));
					}

					time++;

					//printf("after cpu record\n");

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] !=NULL && queue.running[j]->cpu_used == queue.running[j]->cpu_t ){

							fprintf(fd, "terminated\n");

							queue.running[j]->tur_t = (time - queue.running[j]->arrival_t);
							queue.running[j] = NULL;
						}
					}

					//printf("after checking termination\n");

				}//FCFS

				return ret;
			}//end case FCFS

		case NPSJF:
			{

				/*NP SJF scheduling
				 */

				int i = 0;
				int time = 0;

				while(scheduling_done(queue.ready_count, queue.running, arr[i].pid, queue.waiting_count)){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					for(j = 0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->cpu_used == queue.running[j]->io_loc[queue.running[j]->io_interrupted]){

							fprintf(fd, "running to waiting\n");

							queue.running[j]->io_waited = queue.running[j]->io_length[queue.running[j]->io_interrupted];
							queue.running[j]->io_interrupted += 1;
							_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running[j],&queue.waiting_count);
							queue.running[j] = NULL;
						} // running to waiting(I/O interrupt)
					}

					if(queue.waiting_count != 0){

						fprintf(fd, "check waiting list\n");

						NODE* pLoc = queue.waiting_head;
						while(pLoc!=NULL && pLoc->rlink != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]){
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
								pLoc- pLoc->rlink;
							} else if(pLoc->process->io_waited == 0){

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_SJF(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								pLoc = pLoc->rlink;
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink->llink,pLoc->llink, &queue.waiting_count);
							} else { 
								pLoc->process->io_waited -= 1;
								pLoc = pLoc->rlink;
							}

						}// while

						if(pLoc != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]) {
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
							} else if (pLoc->process->io_waited == 0) {

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_SJF(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink,pLoc, &queue.waiting_count);
							} else {
								pLoc->process->io_waited -= 1;
							}
						}
					}//waiting to ready

					if(arr[i].pid != -1 && time == arr[i].arrival_t){

						while(arr[i].pid != -1 && time == arr[i].arrival_t){

							fprintf(fd, "arr to ready \n");

							enque_SJF(&queue.ready_head, &queue.ready_rear, NULL, &arr[i], &queue.ready_count);
							i++;
						}
					}//arr to ready queue

					for(j = 0;j<CPU_CORES;j++){
						if(queue.running[j] == NULL && queue.ready_rear != NULL){

							fprintf(fd, "ready to running\n");

							queue.running[j] = queue.ready_rear->process;
							queue.running[j]->t_wait += (time - queue.running[j]->last_executed);
							_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
						}//ready queue to running
					}


					for(j = 0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL){

							fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running[j]->pid,queue.running[j]->cpu_t, queue.running[j]->cpu_used);

							queue.running[j]->cpu_used += 1;

							//	printf(" after cpu_used increase\n");

							queue.running[j]->last_executed = time;

							//	printf("after recording last_executed\n");
						}
					}

					for(j = 0;j<CPU_CORES;j++){
						_insert(&ret[j].cpu_head, &ret[j].cpu_rear, NULL, queue.running[j], &(ret[j].length));
					}

					time++;

					//printf("after cpu record\n");

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] !=NULL && queue.running[j]->cpu_used == queue.running[j]->cpu_t ){

							fprintf(fd, "terminated\n");

							queue.running[j]->tur_t = (time - queue.running[j]->arrival_t);
							queue.running[j] = NULL;
						}//check termination
					}

					//printf("after checking termination\n");

				}//NPSJF

				return ret;

			}//end case NPSJF 

		case NPPRIO:
			{

				/*NP Priority scheduling
				 */

				int i = 0;
				int time = 0;

				while(scheduling_done(queue.ready_count, queue.running, arr[i].pid, queue.waiting_count)){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					for(j = 0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->cpu_used == queue.running[j]->io_loc[queue.running[j]->io_interrupted]){

							fprintf(fd, "running to waiting\n");

							queue.running[j]->io_waited = queue.running[j]->io_length[queue.running[j]->io_interrupted];
							queue.running[j]->io_interrupted += 1;
							_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running[j],&queue.waiting_count);
							queue.running[j] = NULL;
						} // running to waiting(I/O interrupt)
					}

					if(queue.waiting_count != 0){

						fprintf(fd, "check waiting list\n");

						NODE* pLoc = queue.waiting_head;
						while(pLoc!=NULL && pLoc->rlink != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]){
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
								pLoc- pLoc->rlink;
							} else if(pLoc->process->io_waited == 0){

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_Priority(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								pLoc = pLoc->rlink;
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink->llink,pLoc->llink, &queue.waiting_count);
							} else { 
								pLoc->process->io_waited -= 1;
								pLoc = pLoc->rlink;
							}

						}// while

						if(pLoc != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]) {
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
							} else if (pLoc->process->io_waited == 0) {

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_Priority(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink,pLoc, &queue.waiting_count);
							} else {
								pLoc->process->io_waited -= 1;
							}
						}
					}//waiting to ready

					if(arr[i].pid != -1 && time == arr[i].arrival_t){

						while(arr[i].pid != -1 && time == arr[i].arrival_t){

							fprintf(fd, "arr to ready \n");

							enque_Priority(&queue.ready_head, &queue.ready_rear, NULL, &arr[i], &queue.ready_count);
							i++;
						}
					}//arr to ready queue

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] == NULL && queue.ready_rear != NULL){

							fprintf(fd, "ready to running\n");

							queue.running[j] = queue.ready_rear->process;
							queue.running[j]->t_wait += (time - queue.running[j]->last_executed);
							_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
						}//ready queue to running
					}


					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL){

							fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running[j]->pid,queue.running[j]->cpu_t, queue.running[j]->cpu_used);

							queue.running[j]->cpu_used += 1;

							//	printf(" after cpu_used increase\n");

							queue.running[j]->last_executed = time;

							//	printf("after recording last_executed\n");
						}
					}

					for(j=0;j<CPU_CORES;j++){
						_insert(&ret[j].cpu_head, &ret[j].cpu_rear, NULL, queue.running[j], &(ret[j].length));
					}

					time++;

					//printf("after cpu record\n");

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] !=NULL && queue.running[j]->cpu_used == queue.running[j]->cpu_t ){

							fprintf(fd, "terminated\n");

							queue.running[j]->tur_t = (time - queue.running[j]->arrival_t);
							queue.running[j] = NULL;
						}//check termination
					}

					//printf("after checking termination\n");

				}//NP-priority

				return ret;

			}//end case NP-priority

		case RR:
			{

				/*RR scheduling
				 */

				int i = 0;
				int time = 0;

				while(scheduling_done(queue.ready_count, queue.running, arr[i].pid, queue.waiting_count)){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->cpu_used == queue.running[j]->io_loc[queue.running[j]->io_interrupted]){

							fprintf(fd, "running to waiting\n");

							queue.running[j]->io_waited = queue.running[j]->io_length[queue.running[j]->io_interrupted];
							queue.running[j]->io_interrupted += 1;
							queue.running[j]->rr_ts_used = 0;
							_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running[j],&queue.waiting_count);
							queue.running[j] = NULL;
						} // running to waiting(I/O interrupt)
					}

					for(j = 0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->rr_ts_used == RR_TIMESLICE){
							queue.running[j]->rr_ts_used = 0;
							_insert(&queue.ready_head, &queue.ready_rear,NULL, queue.running[j], &queue.ready_count);
							queue.running[j] = NULL;
						}//rr_ts expired
					}

					if(queue.waiting_count != 0){

						fprintf(fd, "check waiting list\n");

						NODE* pLoc = queue.waiting_head;
						while(pLoc!=NULL && pLoc->rlink != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]){
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
								pLoc- pLoc->rlink;
							} else if(pLoc->process->io_waited == 0){

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								_insert(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								pLoc = pLoc->rlink;
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink->llink,pLoc->llink, &queue.waiting_count);
							} else { 
								pLoc->process->io_waited -= 1;
								pLoc = pLoc->rlink;
							}

						}// while

						if(pLoc != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]) {
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
							} else if (pLoc->process->io_waited == 0) {

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								_insert(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink,pLoc, &queue.waiting_count);
							} else {
								pLoc->process->io_waited -= 1;
							}
						}
					}//waiting to ready

					if(arr[i].pid != -1 && time == arr[i].arrival_t){

						while(arr[i].pid != -1 && time == arr[i].arrival_t){

							fprintf(fd, "arr to ready \n");

							_insert(&queue.ready_head, &queue.ready_rear, NULL, &arr[i], &queue.ready_count);
							i++;
						}
					}//arr to ready queue

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] == NULL && queue.ready_rear != NULL){

							fprintf(fd, "ready to running\n");

							queue.running[j] = queue.ready_rear->process;
							queue.running[j]->t_wait += (time - queue.running[j]->last_executed);
							queue.running[j]->rr_ts_used = 0;
							_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
						}//ready queue to running
					}


					for(j = 0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL){

							fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running[j]->pid,queue.running[j]->cpu_t, queue.running[j]->cpu_used);

							queue.running[j]->cpu_used += 1;
							queue.running[j]->rr_ts_used += 1;

							//	printf(" after cpu_used increase\n");

							queue.running[j]->last_executed = time;

							//	printf("after recording last_executed\n");
						}
					}

					for(j=0;j<CPU_CORES;j++){
						_insert(&ret[j].cpu_head, &ret[j].cpu_rear, NULL, queue.running[j], &(ret[j].length));
					}

					time++;

					//printf("after cpu record\n");

					for(j =0;j<CPU_CORES;j++){
						if(queue.running[j] !=NULL && queue.running[j]->cpu_used == queue.running[j]->cpu_t ){

							fprintf(fd, "terminated\n");

							queue.running[j]->tur_t = (time - queue.running[j]->arrival_t);
							queue.running[j]->rr_ts_used = 0;
							queue.running[j] = NULL;
						}
					}

					//printf("after checking termination\n");

				}//RR

				return ret;
			}//end case RR

		case PSJF:
			{

				/*Preemptive SJF scheduling
				 */

				int i = 0;
				int time = 0;

				while(scheduling_done(queue.ready_count, queue.running, arr[i].pid, queue.waiting_count)){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->cpu_used == queue.running[j]->io_loc[queue.running[j]->io_interrupted]){

							fprintf(fd, "running to waiting\n");

							queue.running[j]->io_waited = queue.running[j]->io_length[queue.running[j]->io_interrupted];
							queue.running[j]->io_interrupted += 1;
							_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running[j],&queue.waiting_count);
							queue.running[j] = NULL;
						} // running to waiting(I/O interrupt)
					}

					if(queue.waiting_count != 0){

						fprintf(fd, "check waiting list\n");

						NODE* pLoc = queue.waiting_head;
						while(pLoc!=NULL && pLoc->rlink != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]){
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
								pLoc- pLoc->rlink;
							} else if(pLoc->process->io_waited == 0){

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_SJF(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								pLoc = pLoc->rlink;
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink->llink,pLoc->llink, &queue.waiting_count);
							} else { 
								pLoc->process->io_waited -= 1;
								pLoc = pLoc->rlink;
							}

						}// while

						if(pLoc != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]) {
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
							} else if (pLoc->process->io_waited == 0) {

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_SJF(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink,pLoc, &queue.waiting_count);
							} else {
								pLoc->process->io_waited -= 1;
							}
						}
					}//waiting to ready

					if(arr[i].pid != -1 && time == arr[i].arrival_t){

						while(arr[i].pid != -1 && time == arr[i].arrival_t){

							fprintf(fd, "arr to ready \n");

							enque_SJF(&queue.ready_head, &queue.ready_rear, NULL, &arr[i], &queue.ready_count);
							i++;
						}
					}//arr to ready queue



					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.ready_rear != NULL && compare_shorter_job(*(queue.ready_rear->process),*queue.running[j])>0 ){

							fprintf(fd, "preemption SJF occured\n");

							enque_SJF(&queue.ready_head, &queue.ready_rear, NULL, queue.running[j], &queue.ready_count);
							queue.running[j]->last_executed = time;
							queue.running[j] = NULL;

						}//preemption SJF
					}


					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] == NULL && queue.ready_rear != NULL){

							fprintf(fd, "ready to running\n");

							queue.running[j] = queue.ready_rear->process;
							queue.running[j]->t_wait += (time - queue.running[j]->last_executed);
							_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
						}//ready queue to running
					}

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL){

							fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running[j]->pid,queue.running[j]->cpu_t, queue.running[j]->cpu_used);

							queue.running[j]->cpu_used += 1;

							//	printf(" after cpu_used increase\n");

							queue.running[j]->last_executed = time;

							//	printf("after recording last_executed\n");
						}
					}

					for(j=0;j<CPU_CORES;j++){
						_insert(&ret[j].cpu_head, &ret[j].cpu_rear, NULL, queue.running[j], &(ret[j].length));
					}

					time++;

					//printf("after cpu record\n");

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] !=NULL && queue.running[j]->cpu_used == queue.running[j]->cpu_t ){

							fprintf(fd, "terminated\n");

							queue.running[j]->tur_t = (time - queue.running[j]->arrival_t);
							queue.running[j] = NULL;
						}//check termination
					}

					//printf("after checking termination\n");

				}//PSJF

				return ret;

			}//end case PSJF 

		case PPRIO:
			{

				/*Preemptive Priority scheduling
				 */

				int i = 0;
				int time = 0;

				while(scheduling_done(queue.ready_count, queue.running, arr[i].pid, queue.waiting_count)){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->cpu_used == queue.running[j]->io_loc[queue.running[j]->io_interrupted]){

							fprintf(fd, "running to waiting\n");

							queue.running[j]->io_waited = queue.running[j]->io_length[queue.running[j]->io_interrupted];
							queue.running[j]->io_interrupted += 1;
							_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running[j],&queue.waiting_count);
							queue.running[j] = NULL;
						} // running to waiting(I/O interrupt)
					}

					if(queue.waiting_count != 0){

						fprintf(fd, "check waiting list\n");

						NODE* pLoc = queue.waiting_head;
						while(pLoc!=NULL && pLoc->rlink != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]){
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
								pLoc- pLoc->rlink;
							} else if(pLoc->process->io_waited == 0){

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_Priority(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								pLoc = pLoc->rlink;
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink->llink,pLoc->llink, &queue.waiting_count);
							} else { 
								pLoc->process->io_waited -= 1;
								pLoc = pLoc->rlink;
							}

						}// while

						if(pLoc != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]) {
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
							} else if (pLoc->process->io_waited == 0) {

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_Priority(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink,pLoc, &queue.waiting_count);
							} else {
								pLoc->process->io_waited -= 1;
							}
						}
					}//waiting to ready

					if(arr[i].pid != -1 && time == arr[i].arrival_t){

						while(arr[i].pid != -1 && time == arr[i].arrival_t){

							fprintf(fd, "arr to ready \n");

							enque_Priority(&queue.ready_head, &queue.ready_rear, NULL, &arr[i], &queue.ready_count);
							i++;
						}
					}//arr to ready queue

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.ready_rear != NULL && compare_priority(*(queue.ready_rear->process), *queue.running[j])>0){

							fprintf(fd, "preemption priority occured\n");

							enque_Priority(&queue.ready_head, &queue.ready_rear, NULL, queue.running[j], &queue.ready_count);
							queue.running[j]->last_executed = time;
							queue.running[j] = NULL;

						}
					}

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] == NULL && queue.ready_rear != NULL){

							fprintf(fd, "ready to running\n");

							queue.running[j] = queue.ready_rear->process;
							queue.running[j]->t_wait += (time - queue.running[j]->last_executed);
							_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
						}//ready queue to running
					}

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL){

							fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running[j]->pid,queue.running[j]->cpu_t, queue.running[j]->cpu_used);

							queue.running[j]->cpu_used += 1;

							//	printf(" after cpu_used increase\n");

							queue.running[j]->last_executed = time;

							//	printf("after recording last_executed\n");
						}
					}

					for(j=0;j<CPU_CORES;j++){
						_insert(&ret[j].cpu_head, &ret[j].cpu_rear, NULL, queue.running[j], &(ret[j].length));
					}

					time++;

					//printf("after cpu record\n");

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] !=NULL && queue.running[j]->cpu_used == queue.running[j]->cpu_t ){

							fprintf(fd, "terminated\n");

							queue.running[j]->tur_t = (time - queue.running[j]->arrival_t);
							queue.running[j] = NULL;
						}//check termination
					}

					//printf("after checking termination\n");

				}//P-priority

				return ret;

			}//end case P-priority

		case CFS:
			{

				/*CFS like scheduling
				 */

				int i = 0;
				int time = 0;

				while(scheduling_done(queue.ready_count, queue.running, arr[i].pid, queue.waiting_count)){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->cpu_used == queue.running[j]->io_loc[queue.running[j]->io_interrupted]){

							fprintf(fd, "running to waiting\n");

							queue.running[j]->io_waited = queue.running[j]->io_length[queue.running[j]->io_interrupted];
							queue.running[j]->io_interrupted += 1;
							queue.running[j]->rr_ts_used = 0;
							_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running[j],&queue.waiting_count);
							queue.running[j] = NULL;
						} // running to waiting(I/O interrupt)
					}

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL && queue.running[j]->rr_ts_used == queue.running[j]->cfs_timeslice){
							queue.running[j]->rr_ts_used = 0;
							enque_vruntime(&queue.ready_head, &queue.ready_rear,NULL, queue.running[j], &queue.ready_count);
							queue.running[j] = NULL;
						}//ts expired
					}

					if(queue.waiting_count != 0){

						fprintf(fd, "check waiting list\n");

						NODE* pLoc = queue.waiting_head;
						while(pLoc!=NULL && pLoc->rlink != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]){
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
								pLoc- pLoc->rlink;
							} else if(pLoc->process->io_waited == 0){

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_vruntime(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								pLoc = pLoc->rlink;
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink->llink,pLoc->llink, &queue.waiting_count);
							} else { 
								pLoc->process->io_waited -= 1;
								pLoc = pLoc->rlink;
							}

						}// while

						if(pLoc != NULL){
							if(pLoc->process->io_waited == 0 && pLoc->process->cpu_used == pLoc->process->io_loc[pLoc->process->io_interrupted]) {
								pLoc->process->io_waited = pLoc->process->io_length[pLoc->process->io_interrupted];
								pLoc->process->io_waited -= 1;
								pLoc->process->io_interrupted += 1;
							} else if (pLoc->process->io_waited == 0) {

								fprintf(fd, "waiting to ready occured\n");

								pLoc->process->last_executed = time;
								enque_vruntime(&queue.ready_head, &queue.ready_rear,NULL,pLoc->process,&queue.ready_count);
								_delete(&queue.waiting_head, &queue.waiting_rear,pLoc->llink,pLoc, &queue.waiting_count);
							} else {
								pLoc->process->io_waited -= 1;
							}
						}
					}//waiting to ready

					if(arr[i].pid != -1 && time == arr[i].arrival_t){

						while(arr[i].pid != -1 && time == arr[i].arrival_t){

							fprintf(fd, "arr to ready \n");

							enque_vruntime(&queue.ready_head, &queue.ready_rear, NULL, &arr[i], &queue.ready_count);
							i++;
						}
					}//arr to ready queue

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] == NULL && queue.ready_rear != NULL){

							fprintf(fd, "ready to running\n");

							queue.running[j] = queue.ready_rear->process;
							queue.running[j]->t_wait += (time - queue.running[j]->last_executed);
							queue.running[j]->rr_ts_used = 0;
							_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
						}//ready queue to running
					}

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] != NULL){

							fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running[j]->pid,queue.running[j]->cpu_t, queue.running[j]->cpu_used);

							queue.running[j]->cpu_used += 1;
							queue.running[j]->rr_ts_used += 1;

							//	printf(" after cpu_used increase\n");

							queue.running[j]->last_executed = time;

							//	printf("after recording last_executed\n");
						}
					}

					for(j=0;j<CPU_CORES;j++){
						_insert(&ret[j].cpu_head, &ret[j].cpu_rear, NULL, queue.running[j], &(ret[j].length));
					}

					time++;

					//printf("after cpu record\n");

					for(j=0;j<CPU_CORES;j++){
						if(queue.running[j] !=NULL && queue.running[j]->cpu_used == queue.running[j]->cpu_t ){

							fprintf(fd, "terminated\n");

							queue.running[j]->tur_t = (time - queue.running[j]->arrival_t);
							queue.running[j]->rr_ts_used = 0;
							queue.running[j] = NULL;
						}
					}

					//printf("after checking termination\n");

				}//CFS

				return ret;
			}//end case CFS

	}//switch

	destroyQueue(queue.ready_head);
	destroyQueue(queue.waiting_head);

}//schedule


EVAL_CPU evaluate(CPU* c, PROCESS* arr, FILE* output){

	EVAL_CPU ret;

	int i = 0;

	ret.total_runtime = 0;
	for(i = 0;i<CPU_CORES;i++){
		ret.cpu_util_time[i] = 0;
		ret.cpu_idle_time[i] = 0;
	}
	ret.total_wait_time = 0;
	ret.avg_wait_time = 0;
	ret.total_tur_time = 0;
	ret.avg_tur_time = 0;

	fprintf(fd, "cpu record length : %d\n", c[i].length);


	for(i = 0;i<CPU_CORES;i++){

		NODE* temp = c[i].cpu_rear;

		fprintf(output, "gantt chart for CPU %d : \n",i+1);
		while(temp != c[i].cpu_head){
			if(temp->process == NULL){
				ret.cpu_idle_time[i] += 1;
				fprintf(output,"0.");
			}
			else {
				ret.cpu_util_time[i] += 1;
				fprintf(output,"%d.",temp->process->pid);
			}
			temp = temp->llink;
		}

		if(temp->process == NULL){
			ret.cpu_idle_time[i] += 1;
			fprintf(output,"0.");
		} else {
			ret.cpu_util_time[i] += 1;
			fprintf(output,"%d.",temp->process->pid);
		}
		fprintf(output,"\n");
	}

	for(i = 0;i<CPU_CORES;i++){
		if(ret.total_runtime < ret.cpu_idle_time[i]+ret.cpu_util_time[i])
			ret.total_runtime = ret.cpu_idle_time[i] + ret.cpu_util_time[i];
	}

	i = 0;
	while(arr[i].pid != -1){
		ret.total_wait_time += arr[i].t_wait;
		ret.total_tur_time += arr[i].tur_t;
		i++;
	}

	ret.avg_wait_time = (float)ret.total_wait_time / i;
	ret.avg_tur_time = (float)ret.total_tur_time / i;

	fprintf(fd, "eval done\n");

	return ret;
}

void printData(EVAL_CPU ec, FILE* output){


	fprintf(output, "total cpu runtime : %d\n", ec.total_runtime);
	for(int i = 0;i<CPU_CORES;i++){
		fprintf(output, "cpu %d idle time : %d\n",i+1 , ec.cpu_idle_time[i]);
		fprintf(output, "cpu %d utilization time : %d\n",i+1 , ec.cpu_util_time[i]);
	}
	fprintf(output, "total process waiting time : %d\n", ec.total_wait_time);
	fprintf(output, "average process waiting time : %f\n", ec.avg_wait_time);
	fprintf(output, "total turnaround time : %d\n", ec.total_tur_time);
	fprintf(output, "average turnaround time : %f\n", ec.avg_tur_time);

}


int main(){

	fd = fopen("./cpuss.log","w+");
	plog = fopen("./process.data","w+");
	FILE* summary = fopen("./cpuss.summary","w+");

	printf("log file is located on ./cpuss.log\n");
	printf("process data is located on ./process.data\n");
	printf("summary of data is also loacated on ./cpuss.summary\n");

	PROCESS arr[PROCESS_MAX+1];

	srand(time(NULL));

	int i;
	for(i=0;i<PROCESS_MAX;i++){
		arr[i] = create_Process();
	}
	arr[i].pid = -1;;

	CPU* c;
	EVAL_CPU ec;


	printf("process initialized, data on ./cpuss.log\n");

	
	int j = 1;
	while(j<8){
		
		printf("\ninitializing Process\n");
		for(i=0;i<PROCESS_MAX;i++)
			resetProcess(&arr[i]);
		printf("=======process initialized=======\n");

		switch(j){
			case 1:
				printf("scheduling array of processes with FCFS, log will be recorded on ./cpuss.log\n");
				fprintf(summary, "FCFS\n\n");
				fprintf(plog,"process data aftere execution of FCFS\n\n");
				break;
			case 2:
				printf("scheduling array of processes with NP-SJF, log will be recorded on ./cpuss.log\n");
				fprintf(summary, "NP-SJF\n\n");
				fprintf(plog,"process data aftere execution of NP-SJF\n\n");
				break;
			case 3:
				printf("scheduling array of processes with NP-Priority, log will be recorded on ./cpuss.log\n");
				fprintf(summary, "NP-Priority\n\n");
				fprintf(plog,"process data aftere execution of NP-Priority\n\n");
				break;
			case 4:
				printf("scheduling array of processes with RR, log will be recorded on ./cpuss.log\n");
				fprintf(summary, "RR\n\n");
				fprintf(plog,"process data aftere execution of RR\n\n");
				break;
			case 5:
				printf("scheduling array of processes with Preemptive SJF, log will be recorded on ./cpuss.log\n");
				fprintf(summary, "Preemptive SJF\n\n");
				fprintf(plog,"process data aftere execution of Preemptive SJF\n\n");
				break;
			case 6:
				printf("scheduling array of processes with Preemptive Priority, log will be recorded on ./cpuss.log\n");
				fprintf(summary, "Preemptive Priority\n\n");
				fprintf(plog,"process data aftere execution of Preemptive Priority\n\n");
				break;
			case 7:
				printf("scheduling array of processes with CFS-like scheduling, log will be recorded on ./cpuss.log\n");
				fprintf(summary, "CFS\n\n");
				fprintf(plog,"process data aftere execution of CFS\n\n");
				break;
		}

		c = schedule(j, arr);
		printf("evaluating CPU usage\n");
		ec = evaluate(c, arr, stdout);
		printData(ec, stdout);


		ec = evaluate(c, arr, summary);
		printData(ec, summary);


		for(i=0;i<PROCESS_MAX;i++)
			printProcessData(plog, &arr[i]);
	
		for(i=0;i<CPU_CORES;i++)
			destroyQueue(c[i].cpu_head);

		j++;
	}// while

	/*

	printf("scheduling array of processes with FCFS, log will be recorded on ./cpuss.log\n");
	c = schedule(FCFS, arr);

	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");

	ec = evaluate(c, arr, stdout);
	printData(ec, stdout);

	fprintf(summary, "FCFS\n\n");
	ec = evaluate(c, arr, summary);
	printData(ec, summary);


	fprintf(plog,"process data aftere execution of FCFS\n\n");
	for(i=0;i<PROCESS_MAX;i++)
		printProcessData(plog, &arr[i]);

	for(i=0;i<CPU_CORES;i++)
		destroyQueue(c[i].cpu_head);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<PROCESS_MAX;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with NP-SJF, log will be recorded on ./cpuss.log\n");
	c = schedule(NPSJF, arr);

	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr, stdout);
	printData(ec, stdout);

	fprintf(summary, "NP-SJF\n\n");
	ec = evaluate(c, arr, summary);
	printData(ec, summary);

	fprintf(plog,"process data aftere execution of NPSJF\n\n");
	for(i=0;i<PROCESS_MAX;i++)
		printProcessData(plog, &arr[i]);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<PROCESS_MAX;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with NP-Priority, log will be recorded on ./cpuss.log\n");
	c = schedule(NPPRIO, arr);

	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr, stdout);
	printData(ec, stdout);

	fprintf(summary, "NP-priority\n\n");
	ec = evaluate(c, arr, summary);
	printData(ec, summary);

	fprintf(plog,"process data aftere execution of NP-priority\n\n");
	for(i=0;i<PROCESS_MAX;i++)
		printProcessData(plog, &arr[i]);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<PROCESS_MAX;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with RR, log will be recorded on ./cpuss.log\n");
	c = schedule(RR, arr);

	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr, stdout);
	printData(ec, stdout);

	fprintf(summary, "RR\n\n");
	ec = evaluate(c, arr, summary);
	printData(ec, summary);

	fprintf(plog,"process data aftere execution of RR\n\n");
	for(i=0;i<PROCESS_MAX;i++)
		printProcessData(plog, &arr[i]);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<PROCESS_MAX;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with Preemptive SJF, log will be recorded on ./cpuss.log\n");
	c = schedule(PSJF, arr);

	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr, stdout);
	printData(ec, stdout);

	fprintf(summary, "Preemptive SJF\n\n");
	ec = evaluate(c, arr, summary);
	printData(ec, summary);

	fprintf(plog,"process data aftere execution of Preemptive SJF\n\n");
	for(i=0;i<PROCESS_MAX;i++)
		printProcessData(plog, &arr[i]);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<PROCESS_MAX;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with Preemptive Priority, log will be recorded on ./cpuss.log\n");
	c = schedule(PPRIO, arr);

	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr, stdout);
	printData(ec, stdout);

	fprintf(summary, "Preemptive Priority\n\n");
	ec = evaluate(c, arr, summary);
	printData(ec, summary);

	fprintf(plog,"process data aftere execution of Preemptive Priority\n\n");
	for(i=0;i<PROCESS_MAX;i++)
		printProcessData(plog, &arr[i]);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<PROCESS_MAX;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with CFS-like scheduling, log will be recorded on ./cpuss.log\n");
	c = schedule(CFS, arr);

	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr, stdout);
	printData(ec, stdout);

	fprintf(summary, "CFS\n\n");
	ec = evaluate(c, arr, summary);
	printData(ec, summary);

	fprintf(plog,"process data aftere execution of CFS\n\n");
	for(i=0;i<PROCESS_MAX;i++)
		printProcessData(plog, &arr[i]);
	*/


	fclose(fd);
	fclose(plog);

	for(i=0;i<PROCESS_MAX;i++)
		remove_Process(&arr[i]);
}












