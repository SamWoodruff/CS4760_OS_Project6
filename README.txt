Samuel Woodruff
5/9/19
Project 5

This program creates user processes and then responds to their requests depending on weather the user returns read, write, or terminate via the message queue. Pagefaults are generated when pages aren't found or swap happens.
To compile:
	Just type make

To execute:
	./oss
	

To execute specify number of concurrently active processes: -n
	./oss -n 7
	This will set the number of concurrently existing processes to 7
	The default is 6, and the max is 18.

