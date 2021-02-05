CC = gcc
CFLAGS = -Wall -g -DDEBUG_MEM_CHECK
LIBS = -lpthread

all: jujube

jujube:base.o buf.o mem.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@ 
%.o:%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY:clean all
clean:
	-rm -rf *.o jujube
