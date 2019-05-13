#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>

#define PROCESS_MAX 200
#define CPU_BIRST_MAX 20
#define IO_BIRST_LENGTH_MAX 10
#define ARRIVAL_TIME_MAX 0
#define PRIORITY_MAX 100
#define IO_INTERRUPT_MAX 0
#define RR_TIMESLICE 5

#define FCFS 1
#define NPSJF 2
#define NPPRIO 3
#define RR 4
#define PSJF 5
#define PPRIO 6

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
	int cpu_util_time;
	int cpu_idle_time;
}EVAL_CPU;

typedef struct process{
	int pid;
	int cpu_t;  //cpu birst time
	int io_t;  // i/o birst time
	int arrival_t; //arival time
	int priority;  //priority
	int io_count;  //number of i/o interrupts

	int io_interrupted;  //internal value for calculating number of i/o intterupts

	int cpu_used;  //internal value for calculating cpu used
	int io_waited;  //internal value for calculate waiting time
	int t_wait;
	int tur_t;

	int rr_ts_used;

	int last_executed;

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
	PROCESS* running;
}QUEUE;

typedef struct cpu{
	int length;
	NODE* cpu_head;
	NODE* cpu_rear;
}CPU;   //for final result evaluation


FILE* fd ;


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

static void printNode(NODE* headPtr){
	while(headPtr!=NULL){
		printf("pid%d->", headPtr->process->pid);
		headPtr++;
	}
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
	ret.priority = rand() % (PRIORITY_MAX+1);

	ret.cpu_used = 0;
	ret.io_interrupted = 0;
	ret.io_waited = 0;
	ret.t_wait = 0;
	ret.tur_t = 0;

	ret.rr_ts_used = 0;

	ret.last_executed = ret.arrival_t;

	ret.io_t = 0;

	//todo::::::::::::::::: make random io locations
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

	return ret;
}//create_Process()

void resetProcess(PROCESS* p){
	p->io_interrupted = 0;
	p->cpu_used = 0;
	p->io_waited = 0;
	p->t_wait = 0;
	p->tur_t=0;
	p->rr_ts_used = 0;

	p->last_executed = p->arrival_t;
}



QUEUE configure(){

	QUEUE ret;
	ret.ready_head = NULL;
	ret.ready_rear = NULL;
	ret.waiting_head = NULL;
	ret.waiting_rear = NULL;
	ret.ready_count = 0;
	ret.waiting_count = 0;
	ret.running = NULL;

	return ret;
}

CPU schedule(int val, PROCESS* arr){   //val, for further input variation, arr : array of processes 

	fprintf(fd, "\n\n start scheduling by val: %d\n\n", val);

	//initializing ret;
	CPU ret;
	ret.cpu_head = NULL;
	ret.cpu_rear = NULL;
	ret.length = 0;

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
	////////////////////////


	QUEUE queue = configure(); 

	fprintf(fd, "end queue configure\n"); //for check


	switch(val){
		case FCFS: 
			{

				/*FCFS scheduling
				 */

				int i = 0;
				int time = 0;

				while(queue.ready_count != 0 || queue.running != NULL || arr[i].pid != -1 || queue.waiting_count != 0){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					if(queue.running != NULL && queue.running->cpu_used == queue.running->io_loc[queue.running->io_interrupted]){

						fprintf(fd, "running to waiting\n");

						queue.running->io_waited = queue.running->io_length[queue.running->io_interrupted];
						queue.running->io_interrupted += 1;
						_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running,&queue.waiting_count);
						queue.running = NULL;
					} // running to waiting(I/O interrupt)

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

					if(queue.running == NULL && queue.ready_rear != NULL){

						fprintf(fd, "ready to running\n");

						queue.running = queue.ready_rear->process;
						queue.running->t_wait += (time - queue.running->last_executed);
						_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
					}//ready queue to running


					if(queue.running != NULL){

						fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running->pid,queue.running->cpu_t, queue.running->cpu_used);

						queue.running->cpu_used += 1;

						//	printf(" after cpu_used increase\n");

						queue.running->last_executed = time;

						//	printf("after recording last_executed\n");
					}

					_insert(&ret.cpu_head, &ret.cpu_rear, NULL, queue.running, &(ret.length));

					time++;

					//printf("after cpu record\n");

					if(queue.running !=NULL && queue.running->cpu_used == queue.running->cpu_t ){

						fprintf(fd, "terminated\n");

						queue.running->tur_t = (time - queue.running->arrival_t);
						queue.running = NULL;
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

				while(queue.ready_count != 0 || queue.running != NULL || arr[i].pid != -1 || queue.waiting_count != 0){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					if(queue.running != NULL && queue.running->cpu_used == queue.running->io_loc[queue.running->io_interrupted]){

						fprintf(fd, "running to waiting\n");

						queue.running->io_waited = queue.running->io_length[queue.running->io_interrupted];
						queue.running->io_interrupted += 1;
						_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running,&queue.waiting_count);
						queue.running = NULL;
					} // running to waiting(I/O interrupt)

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

					if(queue.running == NULL && queue.ready_rear != NULL){

						fprintf(fd, "ready to running\n");

						queue.running = queue.ready_rear->process;
						queue.running->t_wait += (time - queue.running->last_executed);
						_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
					}//ready queue to running


					if(queue.running != NULL){

						fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running->pid,queue.running->cpu_t, queue.running->cpu_used);

						queue.running->cpu_used += 1;

						//	printf(" after cpu_used increase\n");

						queue.running->last_executed = time;

						//	printf("after recording last_executed\n");
					}

					_insert(&ret.cpu_head, &ret.cpu_rear, NULL, queue.running, &(ret.length));

					time++;

					//printf("after cpu record\n");

					if(queue.running !=NULL && queue.running->cpu_used == queue.running->cpu_t ){

						fprintf(fd, "terminated\n");

						queue.running->tur_t = (time - queue.running->arrival_t);
						queue.running = NULL;
					}//check termination

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

				while(queue.ready_count != 0 || queue.running != NULL || arr[i].pid != -1 || queue.waiting_count != 0){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					if(queue.running != NULL && queue.running->cpu_used == queue.running->io_loc[queue.running->io_interrupted]){

						fprintf(fd, "running to waiting\n");

						queue.running->io_waited = queue.running->io_length[queue.running->io_interrupted];
						queue.running->io_interrupted += 1;
						_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running,&queue.waiting_count);
						queue.running = NULL;
					} // running to waiting(I/O interrupt)

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

					if(queue.running == NULL && queue.ready_rear != NULL){

						fprintf(fd, "ready to running\n");

						queue.running = queue.ready_rear->process;
						queue.running->t_wait += (time - queue.running->last_executed);
						_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
					}//ready queue to running


					if(queue.running != NULL){

						fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running->pid,queue.running->cpu_t, queue.running->cpu_used);

						queue.running->cpu_used += 1;

						//	printf(" after cpu_used increase\n");

						queue.running->last_executed = time;

						//	printf("after recording last_executed\n");
					}

					_insert(&ret.cpu_head, &ret.cpu_rear, NULL, queue.running, &(ret.length));

					time++;

					//printf("after cpu record\n");

					if(queue.running !=NULL && queue.running->cpu_used == queue.running->cpu_t ){

						fprintf(fd, "terminated\n");

						queue.running->tur_t = (time - queue.running->arrival_t);
						queue.running = NULL;
					}//check termination

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

				while(queue.ready_count != 0 || queue.running != NULL || arr[i].pid != -1 || queue.waiting_count != 0){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					if(queue.running != NULL && queue.running->cpu_used == queue.running->io_loc[queue.running->io_interrupted]){

						fprintf(fd, "running to waiting\n");

						queue.running->io_waited = queue.running->io_length[queue.running->io_interrupted];
						queue.running->io_interrupted += 1;
						queue.running->rr_ts_used = 0;
						_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running,&queue.waiting_count);
						queue.running = NULL;
					} // running to waiting(I/O interrupt)

					if(queue.running != NULL && queue.running->rr_ts_used == RR_TIMESLICE){
						queue.running->rr_ts_used = 0;
						_insert(&queue.ready_head, &queue.ready_rear,NULL, queue.running, &queue.ready_count);
						queue.running = NULL;
					}//rr_ts expired

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

					if(queue.running == NULL && queue.ready_rear != NULL){

						fprintf(fd, "ready to running\n");

						queue.running = queue.ready_rear->process;
						queue.running->t_wait += (time - queue.running->last_executed);
						queue.running->rr_ts_used = 0;
						_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
					}//ready queue to running


					if(queue.running != NULL){

						fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running->pid,queue.running->cpu_t, queue.running->cpu_used);

						queue.running->cpu_used += 1;
						queue.running->rr_ts_used += 1;

						//	printf(" after cpu_used increase\n");

						queue.running->last_executed = time;

						//	printf("after recording last_executed\n");
					}

					_insert(&ret.cpu_head, &ret.cpu_rear, NULL, queue.running, &(ret.length));

					time++;

					//printf("after cpu record\n");

					if(queue.running !=NULL && queue.running->cpu_used == queue.running->cpu_t ){

						fprintf(fd, "terminated\n");

						queue.running->tur_t = (time - queue.running->arrival_t);
						queue.running->rr_ts_used = 0;
						queue.running = NULL;
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

				while(queue.ready_count != 0 || queue.running != NULL || arr[i].pid != -1 || queue.waiting_count != 0){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					if(queue.running != NULL && queue.running->cpu_used == queue.running->io_loc[queue.running->io_interrupted]){

						fprintf(fd, "running to waiting\n");

						queue.running->io_waited = queue.running->io_length[queue.running->io_interrupted];
						queue.running->io_interrupted += 1;
						_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running,&queue.waiting_count);
						queue.running = NULL;
					} // running to waiting(I/O interrupt)

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



					if(queue.running != NULL && queue.ready_rear != NULL && compare_shorter_job(*(queue.ready_rear->process),*queue.running)>0 ){

						fprintf(fd, "preemption SJF occured\n");

						enque_SJF(&queue.ready_head, &queue.ready_rear, NULL, queue.running, &queue.ready_count);
						queue.running = NULL;

					}//preemption SJF
					


					if(queue.running == NULL && queue.ready_rear != NULL){

						fprintf(fd, "ready to running\n");

						queue.running = queue.ready_rear->process;
						queue.running->t_wait += (time - queue.running->last_executed);
						_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
					}//ready queue to running


					if(queue.running != NULL){

						fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running->pid,queue.running->cpu_t, queue.running->cpu_used);

						queue.running->cpu_used += 1;

						//	printf(" after cpu_used increase\n");

						queue.running->last_executed = time;

						//	printf("after recording last_executed\n");
					}

					_insert(&ret.cpu_head, &ret.cpu_rear, NULL, queue.running, &(ret.length));

					time++;

					//printf("after cpu record\n");

					if(queue.running !=NULL && queue.running->cpu_used == queue.running->cpu_t ){

						fprintf(fd, "terminated\n");

						queue.running->tur_t = (time - queue.running->arrival_t);
						queue.running = NULL;
					}//check termination

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

				while(queue.ready_count != 0 || queue.running != NULL || arr[i].pid != -1 || queue.waiting_count != 0){

					fprintf(fd, "schedule running, next job : arr[%d]  time : %d\n ready: %d waiting: %d\n",i, time, queue.ready_count, queue.waiting_count);

					if(queue.running != NULL && queue.running->cpu_used == queue.running->io_loc[queue.running->io_interrupted]){

						fprintf(fd, "running to waiting\n");

						queue.running->io_waited = queue.running->io_length[queue.running->io_interrupted];
						queue.running->io_interrupted += 1;
						_insert(&queue.waiting_head, &queue.waiting_rear,NULL,queue.running,&queue.waiting_count);
						queue.running = NULL;
					} // running to waiting(I/O interrupt)

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
					
					if(queue.running != NULL && queue.ready_rear != NULL && compare_priority(*(queue.ready_rear->process), *queue.running)>0){

						fprintf(fd, "preemption priority occured\n");

						enque_Priority(&queue.ready_head, &queue.ready_rear, NULL, queue.running, &queue.ready_count);
						queue.running = NULL;
						
					}

					if(queue.running == NULL && queue.ready_rear != NULL){

						fprintf(fd, "ready to running\n");

						queue.running = queue.ready_rear->process;
						queue.running->t_wait += (time - queue.running->last_executed);
						_delete(&queue.ready_head, &queue.ready_rear, queue.ready_rear->llink, queue.ready_rear, &queue.ready_count);
					}//ready queue to running

					if(queue.running != NULL){

						fprintf(fd, "--running-- pid : %d cpu_t : %d cpu_used : %d\n",queue.running->pid,queue.running->cpu_t, queue.running->cpu_used);

						queue.running->cpu_used += 1;

						//	printf(" after cpu_used increase\n");

						queue.running->last_executed = time;

						//	printf("after recording last_executed\n");
					}

					_insert(&ret.cpu_head, &ret.cpu_rear, NULL, queue.running, &(ret.length));

					time++;

					//printf("after cpu record\n");

					if(queue.running !=NULL && queue.running->cpu_used == queue.running->cpu_t ){

						fprintf(fd, "terminated\n");

						queue.running->tur_t = (time - queue.running->arrival_t);
						queue.running = NULL;
					}//check termination

					//printf("after checking termination\n");

				}//P-priority

				return ret;

			}//end case P-priority

	}//switch

}//schedule


EVAL_CPU evaluate(CPU c, PROCESS* arr){

	EVAL_CPU ret;

	ret.total_runtime = 0;
	ret.cpu_util_time = 0;
	ret.cpu_idle_time = 0;
	ret.total_wait_time = 0;
	ret.avg_wait_time = 0;
	ret.total_tur_time = 0;
	ret.avg_tur_time = 0;

	fprintf(fd, "cpu record length : %d\n", c.length);


	NODE* temp = c.cpu_rear;
	if(temp->process == NULL)
		fprintf(fd, "c.cpu_rear null\n");

	while(temp != c.cpu_head){
		ret.total_runtime += 1;
		if(temp->process == NULL){
			ret.cpu_idle_time += 1;
			printf("0.");
		}
		else {
			ret.cpu_util_time += 1;
			printf("%d.",temp->process->pid);
		}
		temp = temp->llink;
	}



	if(temp->process == NULL){
		ret.cpu_idle_time += 1;
		printf("0.");
	} else {
		ret.cpu_util_time += 1;
		printf("%d.",temp->process->pid);
	}
	ret.total_runtime += 1;
	printf("\n");


	int i = 0;
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

void printData(EVAL_CPU ec){


	printf("total cpu runtime : %d\n", ec.total_runtime);
	printf("cpu idle time : %d\n", ec.cpu_idle_time);
	printf("cpu utilization time : %d\n", ec.cpu_util_time);
	printf("total process waiting time : %d\n", ec.total_wait_time);
	printf("average process waiting time : %f\n", ec.avg_wait_time);
	printf("total turnaround time : %d\n", ec.total_tur_time);
	printf("average turnaround time : %f\n", ec.avg_tur_time);

}


int main(){

	fd = fopen("./cpuss.log","w+");
	printf("log file is located on ./cpuss.log\n");

	PROCESS arr[PROCESS_MAX];

	srand(time(NULL));

	int i;
	for(i=0;i<5;i++){
		arr[i] = create_Process();
	}
	arr[i].pid = -1;;

	CPU c;
	EVAL_CPU ec;


	printf("process initialized, data on ./cpuss.log\n");

	printf("scheduling array of processes with FCFS, log will be recorded on ./cpuss.log\n");
	c = schedule(FCFS, arr);
	
	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr);
	printData(ec);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<5;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with NP-SJF, log will be recorded on ./cpuss.log\n");
	c = schedule(NPSJF, arr);
	
	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr);
	printData(ec);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<5;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with NP-Priority, log will be recorded on ./cpuss.log\n");
	c = schedule(NPPRIO, arr);
	
	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr);
	printData(ec);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<5;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with RR, log will be recorded on ./cpuss.log\n");
	c = schedule(RR, arr);
	
	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr);
	printData(ec);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<5;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with Preemptive SJF, log will be recorded on ./cpuss.log\n");
	c = schedule(PSJF, arr);
	
	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr);
	printData(ec);

	printf("===========resetting process data===================\n");
	printf("\n");

	for(i=0;i<5;i++)
		resetProcess(&arr[i]);
	printf("process initialized\n");

	printf("scheduling array of processes with Preemptive Priority, log will be recorded on ./cpuss.log\n");
	c = schedule(PPRIO, arr);
	
	printf("evaluating CPU usage\n");
	printf("gantt chart : \n");
	ec = evaluate(c, arr);
	printData(ec);

	fclose(fd);
}












