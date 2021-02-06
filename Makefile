CC = gcc
CFLAGS = -Wall -g -DDEBUG_MEM_CHECK
LIBS = -lpthread -pthread

all: jujube_client jujube_server

jujube_client:base.o buf.o mem.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@ 

jujube_server:server.o buf.o mem.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@ 

%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY:clean all
clean:
	-rm -rf *.o jujube_client jujube_server
