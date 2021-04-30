CC = gcc
#CFLAGS = -Wall -g -DDEBUG_MEM_CHECK
#CFLAGS = -g -DDEBUG_MEM_CHECK
CFLAGS = -g -DDEBUG_MEM_CHECK -DUSE_FASTLZ
LIBS = -lpthread -pthread

BIN=test

all:$(BIN) fastlz.o ip.o buf.o mem.o comm.o event.o stream.o connection.o client.o

test:client.o fastlz.o ip.o buf.o mem.o comm.o event.o stream.o connection.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY:clean all
clean:
	-rm -rf *.o
