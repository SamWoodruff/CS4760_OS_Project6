#ifndef QUEUE_H_
#define QUEUE_H_
struct Queue{
	int front,rear,size;
	int capacity;
	int *array;
};

struct Queue* createQueue(int capacity);

int queueSize(struct Queue* queue);

int isFull(struct Queue* queue);

int isEmpty(struct Queue* queue);

void enqueue(struct Queue* queue,int controlBlock);

int dequeue(struct Queue* queue);

int front(struct Queue* queue);

int rear (struct Queue* queue);


#endif
