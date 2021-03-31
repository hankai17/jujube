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

#define LOG_MAX_LEN  1024*32
#define cfg_http_buf_size (8*1024)
#define mid_http_buf_size (64*1024)
#define MAX_EVENTS  1024

typedef void timeout_handler_t(int, void *);

typedef struct tcp_stream {
    time_t data_start;
    int64_t data_in;
    int64_t data_out;
    long	data_begin;
    int client_sock;
    int server_sock;

    struct sockaddr_in client_addr;
    struct sockaddr_in6 client_addr6;
    buf_t  *in_buf;
    buf_t  *out_buf;
    int64_t reply_body_size_left;
    buf_t  *work_buf;
    int64_t data_recved;
    int64_t content_len;

    struct {
        unsigned int eof:1;
        unsigned int server_closed:1;
        unsigned int need_close_connect: 1;
    } flags;
} tcp_stream;

typedef struct myevent_s {
    int fd;
    int events;
    void *arg;
    void (*call_back)(int fd, int read_event, int write_event, void *arg);
    int status;
    long last_active;
    timeout_handler_t *thandler;
    tcp_stream *stream;
} myevent_s;

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

tcp_stream* stream_create(int sock, struct sockaddr_storage* paddr);
int stream_flush_out(tcp_stream *s, int size);
int stream_feed_out(tcp_stream *s);

myevent_s* evget(myevent_s *g_events);
void eventset(struct myevent_s *ev, int fd, tcp_stream* s,
              void (*call_back)(int, int, int, void *), void *arg);
void eventadd(int efd, int events, struct myevent_s *ev);
void eventdel(int efd, struct myevent_s *ev);
void set_event_in_out(int efd, myevent_s *ev);
void close_stream(int g_efd, myevent_s* ev, int reason);
void expires_house_keeping(int g_efd, int *pos, myevent_s *g_events, long now, int except_fd);

void init_listensocket(int efd, short port, myevent_s *g_events,
                       jujube_in_addr *jaddr, void (*call_back)(int, int, int, void *));

#endif

