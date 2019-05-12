#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <string.h>
#include <time.h>
#include "sharedMem.h"

void addClock(struct time* time, int sec, int ns);

struct{
	long mtype;
	char msg[10];
}msgbuf;
struct sharedRes *shared;
int shmid;
int toChild;
int toOSS;

//For when a process should request or let go of a resource
const int ACTION_BOUND_B = 1000000;//When this occurs we should either claim a new resource or release already aquired
const int CHANCE_TO_REQUEST = 40;
const int CHANCE_TO_DIE = 70;
int main(int argc, char *argv[]){
	time_t t;
	time(&t);	
	srand((int)time(&t) % getpid());
	//Attaching shared mem
	key_t key;
	key = ftok(".",'a');
	if((shmid = shmget(key,sizeof(struct sharedRes),0666)) == -1){
		printf("In user!");
		perror("shmget");
		exit(0);	
	}
	
	shared = (struct sharedRes*)shmat(shmid,(void*)0,0);
	if(shared == (void*)-1){
		perror("Error on attaching memory");
		exit(1);
	}
	
	//Attach queues for communication between processes
	key_t msgkey;
	if((msgkey = ftok("msgQueue1",925)) == -1){
		perror("ftok");
		exit(1);
	}

	if((toChild = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");
		exit(1);
	}	
	
	if((msgkey = ftok("msgQueue2",825)) == -1){
		perror("ftok");
		exit(1);
	}

	if((toOSS = msgget(msgkey, 0600 | IPC_CREAT)) == -1){
		perror("msgget");	
		exit(1);
	}
	int pid = atoi(argv[0]);
	
	//Setting the time structure for when to release or request
	int timeBetweenActions = (rand() % ACTION_BOUND_B + 1);
	struct time actionTime;
	actionTime.seconds = shared->time.seconds;
	actionTime.nanoseconds = shared->time.nanoseconds;
	addClock(&actionTime, 0, timeBetweenActions);

	//Setting time structure to check for termination
	int termination = (rand() % (250 * 1000000) + 1);//[0,250]ms(converted to ns)
	struct time nextTerminationCheck;
	nextTerminationCheck.seconds = shared->time.seconds;
	nextTerminationCheck.nanoseconds = shared->time.nanoseconds;
	addClock(&nextTerminationCheck, 0, termination);

	int k;
	int count = 0;
	while(1){
		//Check time if we should attempt a request or make a release
		if((shared->time.seconds > actionTime.seconds) || (shared->time.seconds == actionTime.seconds && shared->time.nanoseconds >= actionTime.nanoseconds)){
			//Set next action time
			actionTime.seconds = shared->time.seconds;
			actionTime.nanoseconds = shared->time.nanoseconds;
			addClock(&actionTime, 0, timeBetweenActions);	
	
			if((rand() % 100) < CHANCE_TO_REQUEST){	
				count++;
				strcpy(msgbuf.msg,"REQUEST");
				msgbuf.mtype = pid;
				msgsnd(toOSS,&msgbuf,sizeof(msgbuf),0);

				int request = rand()%32;
				sprintf(msgbuf.msg,"%d",request);

				msgsnd(toOSS,&msgbuf,sizeof(msgbuf),0);
	
				while(1){
					msgrcv(toChild,&msgbuf,sizeof(msgbuf),pid,0);
					if(strcmp(msgbuf.msg, "READGRANTED") == 0){
						break;
					}
				}
			}else{
				count++;
				strcpy(msgbuf.msg,"WRITE");
				msgbuf.mtype = pid;
				msgsnd(toOSS,&msgbuf,sizeof(msgbuf),0);

				int writeRes = rand()%32; 
				sprintf(msgbuf.msg,"%d",writeRes);
				msgsnd(toOSS, &msgbuf,sizeof(msgbuf),0);

				while(1){
					msgrcv(toChild, &msgbuf,sizeof(msgbuf),pid,0);
					if(strcmp(msgbuf.msg,"WRITEGRANTED") == 0){
						break;
					}
				}
			}
		}
		//Checking for termination
		if(((count%100) == 0)	&& count != 0){
			if((rand()%100) <= CHANCE_TO_DIE){
				strcpy(msgbuf.msg,"TERMINATED");
				msgbuf.mtype = pid;
				msgsnd(toOSS,&msgbuf,sizeof(msgbuf),0);	
				exit(0);
			}	
		}	
	}	
}

void addClock(struct time* time, int sec, int ns){
	time->seconds += sec;
	time->nanoseconds += ns;
	while(time->nanoseconds >= 1000000000){
		time->nanoseconds -=1000000000;
		time->seconds++;
	}
}
