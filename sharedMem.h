#ifndef SHAREDMEM_H_
#define SHAREDMEM_H_
//All shared structure will exist here
struct time{
	int nanoseconds;
	int seconds;
};

struct processInfo{
	int pid;
	struct time unblockTime;
	int unblock;
	int frame;
};

struct sharedRes{
	struct time time;
	struct processInfo processes[18];
};
#endif
