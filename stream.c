#include "stream.h"

static tcp_stream* stream_create(int sock, struct sockaddr_storage* paddr)
{
    tcp_stream* s;
    s = mem_alloc("stream", sizeof(tcp_stream));
    memset(s, 0, sizeof(tcp_stream));
    s->data_start = time(NULL);
    s->data_in = 0;
    s->data_out = 0;

    struct timeval now;
    gettimeofday(&now, NULL);
    s->data_begin = ((long)now.tv_sec) * 1000 + (long)now.tv_usec / 1000;

    s->server_sock = sock;
    //memcpy(&s->client_addr, paddr, sizeof(struct sockaddr_in));

    s->in_buf = mem_alloc("http_stream_in_buf",sizeof(buf_t));
    s->in_buf->buffer = mem_alloc("http_stream_in_buf_data", cfg_http_buf_size);
    s->in_buf->buffer[0] = 0;
    s->in_buf->buf_len = cfg_http_buf_size;
    s->in_buf->data_len = 0;

    s->out_buf = mem_alloc("http_stream_out_buf",sizeof(buf_t));
    s->out_buf->buffer = mem_alloc("http_stream_out_buf_data_4", mid_http_buf_size);
    s->out_buf->buf_len = mid_http_buf_size;
    s->out_buf->data_len = 0;

    return s;
}

int stream_flush_out(tcp_stream *s, int size)
{
    int r;
    int t = 0;
    int cur_size = size;

    if(s->server_sock < 0) return -1;
    if(buf_data_size(s->in_buf) < size || size <= 0 )
        cur_size = buf_data_size(s->in_buf);

    while(cur_size > 0) {
        r = write(s->server_sock, s->in_buf->buffer, cur_size);
        if(r < 0) {
            if(errno == EINTR) {
                continue;
            }
            if(errno == EWOULDBLOCK || errno == EAGAIN) {
                return t;
            } else return -1;
        } else if ( r == 0 ) {
            return t;
        } else {
            buf_pick(s->in_buf, r);
            cur_size -= r;
            t += r;
        }
    }
    return t;
}

int stream_feed_out(tcp_stream *s)
{
    int r;
    int t = 0;
    if(s->server_sock < 0 ) return -1;
    while(buf_free_space(s->out_buf) > 0) {
        r = read(s->server_sock, buf_end_position(s->out_buf), buf_free_space(s->out_buf));
        if(r < 0) {
            if(errno == EINTR ) continue;
            if(errno == EWOULDBLOCK || errno == EAGAIN) {
                return t;
            } else {
                return -1;
            }
        } else if( r == 0) { //peer active closed. Its possiable that there were remain datas in buf !
            s->flags.server_closed = 1;
            return -1;
        } else {
            s->out_buf->data_len += r;
            t += r;
            //return t;
            return t;
        }
    }
    s->data_in += t;
    return t;
}

tcp_stream* new_transaction_stream(struct sockaddr_storage *peeraddr, int sock,
        char *prefix_len, char *fast_compress_buff, int compress_ret)
{
    tcp_stream* s = stream_create(sock, peeraddr);
    if (s == NULL) return NULL;
    s->flags.server_closed = 0;

    buf_put(s->in_buf, prefix_len, 8);
    buf_put(s->in_buf, fast_compress_buff, compress_ret);
    return s;
}

void free_transaction_stream(tcp_stream *s)
{
    if (s != NULL) {
        s->server_sock = -1;
        mem_free(s->in_buf->buffer);
        mem_free(s->in_buf);
        mem_free(s->out_buf->buffer);
        mem_free(s->out_buf);
        if (s->work_buf) {
            mem_free(s->work_buf->buffer);
            mem_free(s->work_buf);
        }
        mem_free(s);
    }
    return;
}

