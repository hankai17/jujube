#ifndef _STREAM_H
#define _STREAM_H

#include "ip.h"
#include "buf.h"
#include "mem.h"
#include "connection.h"

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
    struct jconnect *jc;
} tcp_stream, *stream;

//tcp_stream* stream_create(int sock, struct sockaddr_storage* paddr);

tcp_stream* new_transaction_stream(struct sockaddr_storage *peeraddr, int sock,
        char *prefix_len, char *buff, int buff_len);
//void attach_stream_to_connect(myevent_s *g_events, struct jconnect *jc, tcp_stream *s);
//void release_stream_from_connect(int g_efd, myevent_s* ev, int is_bad_conn);
void free_transaction_stream(tcp_stream *s);

int stream_flush_out(tcp_stream *s, int size);
int stream_feed_out(tcp_stream *s);

#endif

