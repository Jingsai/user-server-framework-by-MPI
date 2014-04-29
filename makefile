#
CC = /nfshome/rbutler/public/courses/pp6430/mpich3i/bin/mpicc
CFLAGS = -g
LIBS = -lm 

pstest1: pstest1.c libmy.a
	$(CC) pstest1.c -o pstest1 -lmy -L.

pstest2: pstest2.c libmy.a
	$(CC) pstest2.c -o pstest2 -lmy -L.

pstest3: pstest3.c libmy.a
	$(CC) pstest3.c -o pstest3 -lmy -L.

libmy.a: pp.c queue.c
	$(CC) -c pp.c
	gcc -c queue.c
	ar rvf libmy.a queue.o
	ar rvf libmy.a pp.o

clean:
	rm -f pstest1 pstest2 pstest3 libmy.a  *.o
