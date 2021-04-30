#ifndef _COMM_H
#define _COMM_H
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <assert.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "buf.h"
#include "mem.h"
#include "ip.h"
#include "connection.h"
#include "event.h"
#include "stream.h"

#define LOG_MAX_LEN  1024*32
#define cfg_http_buf_size (8*1024)
#define mid_http_buf_size (64*1024)
#define MAX_EVENTS  1024

#define OK_CONNECT 0
#define BAD_CONNECT 1

struct transfer_log_entry {
    struct list_head list;
    struct jujube_in_addr ip;
    int proto; // 0 udp; 1 tcp
    uint16_t port;
    uint64_t msg_len;
    char gather_logmsg[32 * 1024];
};

const char *xstrerror(void);
int connect_nonb(int  sk,struct sockaddr* addr);
int comm_set_nonblock(int fd);
int set_wake_up_polling(int efd, int tickle_fd[2]);
void wake_up_poll(int tickle_fd[2]);
int create_connect_socket(struct sockaddr_storage *peeraddr, 
        jujube_in_addr jaddr, int ip_flag, int port);

#endif

