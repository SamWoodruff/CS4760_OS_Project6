all: oss user
oss: oss.c
	gcc -o oss oss.c sharedMem.h queue.h queue.c
user: user.c
	gcc -o user user.c
clean:
	-rm oss user
