#include "comm.h"

const char *xstrerror(void)
{
    static char xstrerror_buf[1024 * 4];
    const char *errmsg;

    errmsg = strerror(errno);
    if (!errmsg || !*errmsg)
        errmsg = "Unknown error";

    snprintf(xstrerror_buf, 1024 * 4, "(%d) %s", errno, errmsg);
    return xstrerror_buf;
}

int connect_nonb(int  sk,struct sockaddr* psa)
{
    int r;
    r = connect(sk,psa,sizeof(struct sockaddr));
    if(r < 0 && errno == EINPROGRESS) r = -2;
    return r;
}

int comm_set_nonblock(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

tcp_stream* stream_create(int sock, struct sockaddr_storage* paddr)
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

myevent_s* evget(myevent_s *g_events)
{
    int i = 0;
    myevent_s* ev = NULL;

    for(i = 0; i < MAX_EVENTS; i++) {
        if (g_events[i].status == 0) {
            ev = &g_events[i];
            break;
        }
    }
    if(i == MAX_EVENTS) {
        printf("max fd limit\n");
    }
    return ev;
}

void eventset(struct myevent_s *ev, int fd, tcp_stream* s,
              void (*call_back)(int, int, int, void *), void *arg)
{
    ev->fd = fd;
    ev->call_back = call_back;
    ev->events = 0;
    ev->arg = arg;
    ev->status = 0;
    ev->last_active = time(NULL);
    ev->stream = s;
    return;
}

void eventadd(int efd, int events, struct myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};
    int op;
    ev->last_active = time(NULL);
    epv.data.ptr = ev;
    epv.events = ev->events = events;

    if (ev->status == 1) {
        op = EPOLL_CTL_MOD;
    } else {
        op = EPOLL_CTL_ADD;
        ev->status = 1;
    }

    if (epoll_ctl(efd, op, ev->fd, &epv) < 0) {
        //printf("eventadd failed fd: %d, events: %d, op: %d, err: %s\n", ev->fd, events, op,  xstrerror());
    } else {
        ;
        //printf("eventadd OK [fd=%d], op=%d, events[%0X]\n", ev->fd, op, events);
    }

    return;
}

void eventdel(int efd, struct myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};
    if (ev->status != 1)
        return;

    epv.data.ptr = ev;
    ev->status = 0;
    if(epoll_ctl(efd, EPOLL_CTL_DEL, ev->fd, &epv) < 0) {
        //printf("eventdell fd: %d, err: %s\n", ev->fd, xstrerror());
    } else {
        ;
        //printf("client eventdell fd: %d, ok\n", ev->fd);
    }
    return;
}

void set_event_in_out(int g_efd, myevent_s *ev)
{
    tcp_stream* s = ev->stream;
    int rw_type = 0;
    if(s->server_sock < 0)
        return;
    if(buf_free_space(s->out_buf) > 0) {
        rw_type |= EPOLLIN;
    }
    if(buf_data_size(s->in_buf) > 0) {
        rw_type |= EPOLLOUT;
    }
    //eventdel(g_efd, ev);
    //eventset(ev, ev->fd, ev->si, data_transfer, ev);
    eventadd(g_efd, rw_type, ev);

    return;
}

void close_stream(int g_efd, myevent_s* ev, int reason)
{
    if(ev != NULL) {
        tcp_stream* s = ev->stream;
        //printf("server close_stream, s->reply_body_size_left: %ld\n", 
        //      s->reply_body_size_left);
        if(s->server_sock >= 0) {
            eventdel(g_efd, ev);
            close(ev->fd);
            s->server_sock = -1;
        }

        //memset(&s->flags, 0, sizeof(int));
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

void init_listensocket(int efd, short port, myevent_s *g_events,
                       jujube_in_addr *jaddr, void (*accept_cb)(int, int, int, void *))
{
    struct sockaddr_storage peeraddr;
    int lfd = socket(jaddr->flag == USE_IPV6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    fcntl(lfd, F_SETFL, O_NONBLOCK);

    eventset(&g_events[MAX_EVENTS], lfd, NULL, accept_cb, &g_events[MAX_EVENTS]);
    eventadd(efd, EPOLLIN, &g_events[MAX_EVENTS]);

    if (jaddr->flag == USE_IPV6) {
        struct sockaddr_in6 *peer_ipv6_addr = (struct sockaddr_in6 *)&peeraddr;
        peer_ipv6_addr->sin6_family = AF_INET6;
        peer_ipv6_addr->sin6_port = htons(port);
        memcpy(&peer_ipv6_addr->sin6_addr, &jaddr->inx_addr.ipv6_addr,
               sizeof(struct sockaddr_in6));
    } else {
        struct sockaddr_in *peer_ipv4_addr = (struct sockaddr_in *)&peeraddr;
        peer_ipv4_addr->sin_family = AF_INET;
        peer_ipv4_addr->sin_port = htons(port);
        memcpy(&peer_ipv4_addr->sin_addr, &jaddr->inx_addr.ipv4_addr,
               sizeof(struct sockaddr_in));
    }

    bind(lfd, (struct sockaddr *)&peeraddr, jaddr->flag == USE_IPV6 ?
                                            sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in));
    listen(lfd, 128);
    return;
}

void expires_house_keeping(int g_efd, int *pos, myevent_s *g_events,
                           long now, int except_fd)
{
    int checkpos = *pos;
    int i = 0;
    for (; i < 100; i++, checkpos++) {
        if (checkpos == MAX_EVENTS)
            checkpos = 0;
        if (g_events[checkpos].status != 1)
            continue;
        long duration = now - g_events[checkpos].last_active;
        if (duration >= 10 && except_fd != g_events[checkpos].fd) {
            eventdel(g_efd, &g_events[checkpos]);
            close_stream(g_efd, &g_events[checkpos], 0);
        }
    }
    *pos = checkpos;
}

