CC = gcc
#CFLAGS = -Wall -g -DDEBUG_MEM_CHECK
#CFLAGS = -g -DDEBUG_MEM_CHECK
CFLAGS = -g -DDEBUG_MEM_CHECK -DUSE_FASTLZ
LIBS = -lpthread -pthread

all: fastlz.o ip.o buf.o mem.o comm.o server.o client.o 

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY:clean all
clean:
	-rm -rf *.o
