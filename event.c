#include "event.h"

myevent_s* evget(myevent_s *g_events, int fd)
{
    int i = 0;
    myevent_s* ev = NULL;

    for(i = 0; i < MAX_EVENTS; i++) {
        if (g_events[i].status == 0 && fd == -1) {
            ev = &g_events[i];
            break;
        } else if (fd != -1 && 
                g_events[i].jc &&
                g_events[i].jc->fd == fd) {
            ev = &g_events[i];
            break;
        }
    }
    if(i == MAX_EVENTS) {
        printf("max fd limit\n");
    }
    return ev;
}

void eventset(struct myevent_s *ev, struct jconnect *jc,
              void (*call_back)(int, int, int, void *), void *arg)
{
    ev->jc = jc;
    ev->fd = jc->fd;
    ev->call_back = call_back;
    ev->events = 0;
    ev->arg = arg;
    ev->status = 0;
    ev->last_active = time(NULL);
    return;
}

void event_reset_cb(struct myevent_s *ev, 
        void (*call_back)(int, int, int, void *), void *arg) {
    ev->call_back = call_back;
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

void set_stream_event_in_out(int g_efd, myevent_s *ev)
{
    tcp_stream* s = ev->jc->stream;
    assert(s); // Only used for txn's stream
    int rw_type = 0;
    if (s->server_sock < 0)
        return;
    if (buf_free_space(s->out_buf) > 0) {
        rw_type |= EPOLLIN;
    }
    if (buf_data_size(s->in_buf) > 0) {
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
        if (ev->jc) {
            tcp_stream* s = ev->jc->stream;
            if (s != NULL) { // activate timeout
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
            } else { // keep alive(inactivate) timeout
                // it's a raw connection. Do not touch it in there.
                // We should use connect_pool mgmt it.
            }
        }
    }
    return;
}

void init_listensocket(int efd, short port, myevent_s *g_events,
                       jujube_in_addr *jaddr, void (*accept_cb)(int, int, int, void *))
{
    struct sockaddr_storage peeraddr;
    int lfd = socket(jaddr->flag == USE_IPV6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    fcntl(lfd, F_SETFL, O_NONBLOCK);

    struct jconnect *jc = (jconnect*)malloc(sizeof(struct jconnect));
    if (jc == NULL) return;
    jc->fd = lfd;

    eventset(&g_events[MAX_EVENTS], jc, accept_cb, &g_events[MAX_EVENTS]);
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
    //for (; i < 100; i++, checkpos++) {
    for (; i < MAX_EVENTS + 1; i++, checkpos++) {
        if (checkpos == MAX_EVENTS)
            checkpos = 0;
        if (g_events[checkpos].status != 1)
            continue;
        long duration = now - g_events[checkpos].last_active;
        //printf("fd: %d, duration: %d\n", g_events[checkpos].fd, duration);
        if (duration >= 10 && except_fd != g_events[checkpos].fd) {
            eventdel(g_efd, &g_events[checkpos]);
            close_stream(g_efd, &g_events[checkpos], 0);
        }
    }
    *pos = checkpos;
}

