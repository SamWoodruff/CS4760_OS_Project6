#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <string.h>
#include <sys/file.h>
#include "sharedMem.h"
#include "queue.h"

void addClock(struct time* time, int sec, int ns);
void cleanUp();
void ossExit(int sig);
int getProcessId();
void startOSS();
void shiftReference();
void printResources();
void insert(int pageId, int pid);

FILE *fp;
int lines = 0;
int toChild;
int toOSS;
int shmid;
int takenPids[18];

struct{
	long mtype;
	char msg[10];
}msgbuf;

struct Page{
	int frame;
	int swap;
};

struct PageTable{
	struct Page frames[32];
};

struct Frame{
	int pid;
	unsigned dirtyBit : 1;//1 bit
	unsigned ref : 8;//8 bit
};

struct Memory{
	struct Frame frameTable[256];
	struct PageTable pageTables[18];//A page table for each potential process
};

struct sharedRes *shared;
struct Queue *waiting; 
struct Memory memory;


int pageFaults;
int requests;
int n = 0;//Number of processes in system

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte) \
	(byte & 0x80 ? '1' : '0'), \
	(byte & 0x40 ? '1' : '0'), \
	(byte & 0x20 ? '1' : '0'), \
	(byte & 0x10 ? '1' : '0'), \
	(byte & 0x08 ? '1' : '0'), \
	(byte & 0x04 ? '1' : '0'), \
	(byte & 0x02 ? '1' : '0'), \
	(byte & 0x01 ? '1' : '0')
int main(int argc, char *argv[]){
	int c;	
	while((c=getopt(argc, argv, "n:h"))!= EOF){
		switch(c){
			case 'h':
				printf("-v: Set v to 1 for verbose on, or to 0 for verbose off.\n");
				exit(0);
				break;
			case 'n':
				n = atoi(optarg);
				if(n > 18){
					printf("n can be no greater than 18. Setting variable to 18 now.\n");
					n = 18;
				}
				break;
		}
	}

	if(n == 0){
		n = 6;
	}
	srand(time(NULL));
	//Attach shared memory	
	key_t key;
	key = ftok(".",'a');
	if((shmid = shmget(key,sizeof(struct sharedRes), IPC_CREAT | 0666)) == -1){		
		perror("shmget");
		exit(1);	
	}
	
	shared = (struct sharedRes*)shmat(shmid,(void*)0,0);
	if(shared == (void*)-1){
		error("Error on attaching memory");
	}

	//Attach queues for communication between processes
	key_t msgkey;
	if((msgkey = ftok("msgQueue1",925)) == -1){
		perror("ftok");
	}

	if((toChild = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");	
	}	
	
	if((msgkey = ftok("msgQueue2",825)) == -1){
		perror("ftok");
	}

	if((toOSS = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");	
	}

	fp = fopen("osslog.txt","w");
	
	printf("OSS will be done momentarily\n");
	printf("Generating ouput in osslog.txt now...\n");
	
	//Setting default values in table
	int i,k;
	for(i = 0; i < 256; i++){
		memory.frameTable[i].ref = 0x0;
		memory.frameTable[i].dirtyBit = 0x0;
		memory.frameTable[i].pid = -1;
	}
	
	for(i = 0; i < 18; i++){
		for(k = 0; k < 32; k++){
			memory.pageTables[i].frames[k].frame = -1;
			memory.pageTables[i].frames[k].swap = -1;
		}
	}

	//Setting up signals
	signal(SIGALRM, ossExit);
	alarm(2);
	signal(SIGINT, ossExit);

	startOSS();
	cleanUp(shmid, shared);
}



void startOSS(){
	int i = 0;
	struct time randFork;
	int nextFork = (rand() % (500000000 - 1000000 + 1)) + 1000000;
	addClock(&randFork,0,nextFork);
	int active = 0;
	int count = 0;
	int maxExecs = 25;
	pid_t pids[18];
	//To store procceses that are waiting on a resource
	waiting = createQueue(n);
	pid_t child;
	while(1){
		//increment clock
		addClock(&shared->time,0,10000);
		if(/*(maxExecs > 0)&&*/(active < n) && ((shared->time.seconds > randFork.seconds) || (shared->time.seconds == randFork.seconds && shared->time.nanoseconds >= randFork.nanoseconds))){
			
			//set time for next fork
			randFork.seconds = shared->time.seconds;
			randFork.nanoseconds = shared->time.nanoseconds;
			nextFork = (rand() % (500000000 - 1000000 + 1)) + 1000000;
			addClock(&randFork,0,nextFork);
			int newProc = getProcessId() + 1;
			
			if((newProc-1 > -1)){
				if(lines < 10000){
					fprintf(fp,"Master: Generated new process with PID %d at time %d:%d\n",newProc,shared->time.seconds,shared->time.nanoseconds);
					lines++;
				}
				pids[newProc - 1] = fork();
				if(pids[newProc - 1] == 0){
					char str[10];
					sprintf(str, "%d", newProc);
					execl("./user",str,NULL);
					exit(0);
				}
				maxExecs--;			
				active++;
			}	
		}

		if(msgrcv(toOSS, &msgbuf, sizeof(msgbuf),0,IPC_NOWAIT) > -1){
			int pid = msgbuf.mtype;
			if (strcmp(msgbuf.msg, "TERMINATED") == 0){
				while(waitpid(pids[pid - 1],NULL, 0) > 0);
				int m;
				takenPids[pid - 1] = 0;		
				count++;
				active--;
				if(lines < 10000){
					fprintf(fp,"Master: Process with PID %d has terminated at time %d:%d\n",pid,shared->time.seconds,shared->time.nanoseconds);		
					lines++;
				}
			}
			else if (strcmp(msgbuf.msg, "WRITE") == 0){
				requests++; 
				//Recieve resource to release
				msgrcv(toOSS, & msgbuf, sizeof(msgbuf),pid,0);
				int writePos = atoi(msgbuf.msg);
				if(lines < 10000){
					fprintf(fp,"Master: Detected write request for %d from PID: %d at time %d:%d\n",writePos,pid,shared->time.seconds,shared->time.nanoseconds);
					lines++;
				}
				if(memory.pageTables[pid - 1].frames[writePos].frame == -1){
					if(lines > 10000){
						fprintf(fp,"Master: Page fault(Page not found)-Page %d for PID: %d at time %d:%d\n",writePos,pid,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
					pageFaults++;
					shared->processes[pid - 1].pid = pid-1;
					shared->processes[pid - 1].unblock = 1;
					shared->processes[pid - 1].frame = writePos;
					
					//Each read/request taking about 15ms
					int time = (rand() & (150000000 - 10000000 + 1)) + 10000000;
					addClock(&shared->processes[pid-1].unblockTime,0,time);
					enqueue(waiting,pid);

				}else if(memory.pageTables[pid - 1].frames[writePos].swap == 0){	
					if(lines < 10000){
						fprintf(fp,"Master: Write request granted for PID: %d at time %d:%d\n",pid,shared->time.seconds,shared->time.nanoseconds);				
						lines++;
					}
					strcpy(msgbuf.msg,"WRITEGRANTED");
					msgbuf.mtype = pid;
					addClock(&shared->time,0,100000);
					int framePos = memory.pageTables[pid - 1].frames[writePos].frame;
					memory.frameTable[framePos].dirtyBit = 0x1;
					memory.frameTable[framePos].ref = memory.frameTable[framePos].ref | 0x80;
					msgsnd(toChild, &msgbuf,sizeof(msgbuf),IPC_NOWAIT);
				}else if(memory.pageTables[pid - 1].frames[writePos].swap == 1){
					if(lines < 10000){
						fprintf(fp,"Master: Page fault(Swap) - Page: %d for PID: %d at time %d:%d\n",writePos,pid,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
					pageFaults++;
					shared->processes[pid - 1].pid = pid-1;
					shared->processes[pid - 1].unblock = 1;
					shared->processes[pid - 1].frame = writePos;
					
					//Each read/request taking about 15ms
					int time = (rand() & (150000000 - 10000000 + 1)) + 10000000;
					addClock(&shared->processes[pid-1].unblockTime,0,time);
					enqueue(waiting,pid);
					
				}
			}
			else if (strcmp(msgbuf.msg, "REQUEST") == 0){
				requests++; 
				//Get resource from user
				msgrcv(toOSS,&msgbuf,sizeof(msgbuf),pid,0);	
				int pageId = atoi(msgbuf.msg);	
				if(lines < 10000){
					fprintf(fp,"Master: Detected read request for %d from PID: %d at time %d:%d\n",pageId,pid,shared->time.seconds,shared->time.nanoseconds);
					lines++;
				}
				//Frame exists in memory
				if(memory.pageTables[pid - 1].frames[pageId].frame == -1){
					if(lines < 10000){
						fprintf(fp,"Master: Page fault(Page not found)-Page %d for PID: %d at time %d:%d\n",pageId,pid,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
					pageFaults++;
					shared->processes[pid - 1].pid = pid-1;
					shared->processes[pid - 1].unblock = 0;
					shared->processes[pid - 1].frame = pageId;
					//Each read/request taking about 15ms
					int time = (rand() & (150000000 - 10000000 + 1)) + 10000000;
					addClock(&shared->processes[pid-1].unblockTime,0,time);
					enqueue(waiting,pid);

				}else if(memory.pageTables[pid - 1].frames[pageId].swap == 0){	
					if(lines < 10000){
						fprintf(fp,"Master: Read Request granted for PID: %d at time %d:%d\n",pid,shared->time.seconds,shared->time.nanoseconds);				
						lines++;
					}
					strcpy(msgbuf.msg,"READGRANTED");
					msgbuf.mtype = pid;
					addClock(&shared->time,0,10);	
					int framePos = memory.pageTables[pid - 1].frames[pageId].frame;	
					memory.frameTable[framePos].ref = memory.frameTable[framePos].ref | 0x80;
					msgsnd(toChild, &msgbuf,sizeof(msgbuf),IPC_NOWAIT);
				}else if(memory.pageTables[pid - 1].frames[pageId].swap == 1){
					if(lines < 10000){
						fprintf(fp,"		Master: Page fault(Swap) - Page: %d for PID: %d at time %d:%d\n",pageId,pid,shared->time.seconds,shared->time.nanoseconds);
						lines++;
					}
					pageFaults++;
					shared->processes[pid - 1].pid = pid-1;
					shared->processes[pid - 1].unblock = 0;
					shared->processes[pid - 1].frame = pageId;
					
					//Each read/request taking about 15ms
					int time = (rand() & (150000000 - 10000000 + 1)) + 10000000;
					addClock(&shared->processes[pid-1].unblockTime,0,time);
					enqueue(waiting,pid);
					
				}
				
			}
			if(((requests%100)==0) && requests != 0){
				shiftReference();
				printResources();
			}
		} 
		
		//Check if waiting processes can be given a resource	
		int k = 0;
		if(isEmpty(waiting) == 0){
			int size = queueSize(waiting);
			while(k < size){
				int pid = dequeue(waiting);
				if(((shared->time.seconds > shared->processes[pid-1].unblockTime.seconds) || (shared->time.seconds == shared->processes[pid-1].unblockTime.seconds && shared->time.nanoseconds >= shared->processes[pid-1].unblockTime.nanoseconds))){		
					//Get page requested from shared memory			
					int pageId = shared->processes[pid - 1].frame;
					//Setting reference byte
					int framePos = memory.pageTables[pid - 1].frames[pageId].frame;	
					memory.frameTable[framePos].ref = memory.frameTable[framePos].ref | 0x80;
					
					if(shared->processes[pid-1].unblock == 0){
						if(memory.pageTables[pid - 1].frames[pageId].frame == -1 || memory.pageTables[pid - 1].frames[pageId].swap == 1){
							//insert page			
							insert(pageId,pid);
						}

						msgbuf.mtype = pid;
						strcpy(msgbuf.msg,"READGRANTED");
						msgsnd(toChild,&msgbuf,sizeof(msgbuf),IPC_NOWAIT);
						if(lines < 10000){
							fprintf(fp,"Master: Read granted on page: %d for PID: %d at time %d:%d\n",pageId,pid,shared->time.seconds,shared->time.nanoseconds);
							lines++;
						}
					}else if(shared->processes[pid-1].unblock == 1){
						//Setting dirty bit for the write
						memory.frameTable[framePos].dirtyBit = 0x1;
						if(memory.pageTables[pid - 1].frames[pageId].frame == -1 || memory.pageTables[pid - 1].frames[pageId].swap == 1){
							//insert page
							insert(pageId,pid);

						}

						msgbuf.mtype = pid;
						strcpy(msgbuf.msg,"WRITEGRANTED");
						msgsnd(toChild,&msgbuf,sizeof(msgbuf),IPC_NOWAIT);
						if(lines < 10000){
							fprintf(fp,"Master: Write granted on page: %d for PID: %d at time %d:%d\n",pageId,pid,shared->time.seconds,shared->time.nanoseconds);
							lines++;
						}
					}
				}else{
					enqueue(waiting,pid);
				}
				k++;
			}
		}
	}
	printf("Out of simulation\n");
	int status;
	pid_t wpid;//wait for any children that haven't quite finished yet
	while((wpid = wait(&status)) > 0);
	
}

void insert(int pageId, int pid){
	int i;
	struct Frame temp;
	temp.ref = 0xff;
	int tempPos = -1;
	
	for(i = 0; i < 256; i++){
		//Frame is empty
		if(memory.frameTable[i].pid == -1){
			tempPos = i;
			break;
		}
		if(memory.frameTable[i].ref < temp.ref){
			temp.ref = memory.frameTable[i].ref;
			tempPos = i;	
		}
	}
	if(tempPos == -1){
		tempPos = 0;
	}
	if(memory.frameTable[tempPos].pid > -1){
		fprintf(fp,"Swapping frame %d to %d for PID: %d at time %d:%d\n",tempPos, memory.frameTable[tempPos].pid, pid, shared->time.seconds,shared->time.nanoseconds);
		if(memory.frameTable[tempPos].dirtyBit == 0x1){
			addClock(&shared->time,0,100000);//Dirty bit detected (takes more time)
		}
		memory.pageTables[pid - 1].frames[pageId].swap = 1;
	}

	//Clear memory
	memory.frameTable[tempPos].ref =0x0;
	memory.frameTable[tempPos].dirtyBit = 0x0;
	memory.frameTable[tempPos].pid = - 1;

	memory.frameTable[tempPos].pid = pid;
	memory.frameTable[tempPos].ref = memory.frameTable[tempPos].ref | 0x80;

	memory.pageTables[pid-1].frames[pageId].swap = 0;
	memory.pageTables[pid-1].frames[pageId].frame = tempPos;
}

void shiftReference(){
	int i;
	for(i = 0; i < 256; i++){
		memory.frameTable[i].ref = memory.frameTable[i].ref >> 1;
	}
}


void printResources(){
	if(lines < 10000){
		fprintf(fp,"\nCurrent Memory layout at time %d:%d\n",shared->time.seconds,shared->time.nanoseconds);
		fprintf(fp,"\t     Occupied\tPID\tRef\t   DirtyBit\n");
		lines = lines + 2;
	}
	int i;
	for(i = 0; i < 256; i++){
			if(lines < 10000){	
				fprintf(fp,"Frame %d:\t",i + 1);
				if(memory.frameTable[i].pid != -1){
					fprintf(fp,"Yes\t");
				}else{
					fprintf(fp,"No\t");
				}
				fprintf(fp,"%d\t",memory.frameTable[i].pid);
				fprintf(fp,"%c%c%c%c%c%c%c%c\t",BYTE_TO_BINARY(memory.frameTable[i].ref));
				fprintf(fp,"%x\t",memory.frameTable[i].dirtyBit);
				fprintf(fp,"\n");		
				lines++;
			}
	}
	if(lines < 10000){
		fprintf(fp,"\n");
	}

}

int getProcessId(){
	int i;
	int allReleased = 0;

	for(i = 0; i < 18; i++){
		if(takenPids[i] == 0){
			takenPids[i] = 1;
			return i;
		}
	}
	return -1;
}

//Adds time to the clock structure
void addClock(struct time* time, int sec, int ns){
	time->seconds += sec;
	time->nanoseconds += ns;
	while(time->nanoseconds >= 1000000000){
		time->nanoseconds -=1000000000;
		time->seconds++;
	}
}

//Starts the clean up process for OSS
void ossExit(int sig){
	float avgPerSecond = ((float)(requests)/((float)(shared->time.seconds)+((float)shared->time.nanoseconds/(float)(1000000000))));
	float faultsPerAccess = ((float)(pageFaults)/(float)requests);
	float aveAccTime = (((float)(shared->time.seconds)+((float)shared->time.nanoseconds/(float)(1000000000)))/((float)requests));
	
	printf("Page faults: %d\n",pageFaults);
	printf("Requests: %d\n",requests);
	printf("Memory access  per second: %f\n",avgPerSecond);	
	printf("Page faults per access: %f\n",faultsPerAccess);
	printf("Average access time: %f\n",aveAccTime);

	fprintf(fp,"Page faults: %d\n",pageFaults);
	fprintf(fp,"Requests: %d\n",requests);
	fprintf(fp,"Memory access  per second: %f\n",avgPerSecond);	
	fprintf(fp,"Page faults per access: %f\n",faultsPerAccess);
	fprintf(fp,"Average access time: %f\n",aveAccTime);

	switch(sig){
		case SIGALRM:
			printf("\n2 seconds  has passed. The program will now terminate.\n");
			break;
		case SIGINT:
			printf("\nctrl+c has been registered. Now exiting.\n");
			break;
	}
	cleanUp();
	kill(0,SIGKILL);
}

//Remove shared structures
void cleanUp(){
	fclose(fp);
	msgctl(toOSS,IPC_RMID,NULL);
	msgctl(toChild,IPC_RMID,NULL);
	shmdt((void*)shared);	
	shmctl(shmid, IPC_RMID, NULL);
}
