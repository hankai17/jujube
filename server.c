#include "comm.h"
#include "fastlz.h"

static myevent_s g_events[MAX_EVENTS + 1];
static jujube_in_addr jaddr;
static int g_efd;
static uint64_t g_qps = 0;

void main_rcv_loop(void *ip, int port);
static void data_transfer(int sock, int read_event, int write_event, void *data);

static void accept_conn(int lfd, int read_events, int write_event, void *arg)
{
    struct sockaddr_storage cin;
    socklen_t len = sizeof(cin);
    int cfd, i;

    if ((cfd = accept(lfd, (struct sockaddr *)&cin, &len)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
        }
        return;
    }

    do {
        for (i = 0; i < MAX_EVENTS; i++) {
            if (g_events[i].status == 0)
                break;
        }

        if (i == MAX_EVENTS) {
            break;
        }

        int flag = 0;
        if ((flag = fcntl(cfd, F_SETFL, O_NONBLOCK)) < 0) {
            break;
        }

        tcp_stream* s = stream_create(cfd, &cin);
        struct myevent_s* ev = evget(g_events);
        eventset(ev, cfd, s, data_transfer, ev);
        eventadd(g_efd, EPOLLIN, ev);

    } while(0);
    
    // Check fd or stream vaild TODO

#if 0 // TODO
    if (jaddr.flag == USE_IPV4) {
        struct sockaddr_in *cin4 = (struct sockaddr_in *)&cin;
        printf("accept new connect [%s:%d][time:%ld], pos[%d]\n", 
                inet_ntoa(cin4->sin_addr), ntohs(cin4->sin_port), g_events[i].last_active, i);
    } else {
        struct sockaddr_in6 *cin6 = (struct sockaddr_in6 *)&cin;
        printf("accept new connect [%s:%d][time:%ld], pos[%d]\n", 
                "todo", ntohs(cin6->sin6_port), g_events[i].last_active, i);
    }
#endif

    return;
}

void main_rcv_loop(void *ip, int port) {
    g_efd = epoll_create(MAX_EVENTS + 1);
    if(g_efd <= 0) {
        return;
    }

    memcpy(&jaddr, ip, sizeof(struct jujube_in_addr));
    init_listensocket(g_efd, port, g_events, &jaddr, accept_conn);

    struct epoll_event events[MAX_EVENTS + 1];
    int checkpos = 0, i;
    signal(SIGPIPE, SIG_IGN);
    long next_time = time(NULL) + 10;

    while(1) {
        long now = time(NULL);

        // periodicity task
        if (now >= next_time) {
            //printf("--------------------------------------statics: %d/10s\n", g_qps);
            g_qps = 0;
            next_time = now + 10;
        }
        expires_house_keeping(g_efd, &checkpos, g_events, now, -1);
        int nfd = epoll_wait(g_efd, events, MAX_EVENTS + 1, 1000);
        if (nfd < 0 && errno != EINTR) {
            break;
        }
        for (i = 0; i < nfd; i++) {
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;
            ev->call_back(ev->fd, events[i].events & EPOLLIN ? 1 : 0,
                          events[i].events & EPOLLOUT ? 1 : 0, ev->arg);
        }
    }
    close(g_efd);
    return;
}

static int event_set_timeout(struct myevent_s *ev, int timeout,
                             timeout_handler_t handler)
{
    ev->thandler = handler;
    return 0;
}

extern do_work(char *buffer, int len);

/*
** return 0: need more; 1 recved done; -1 failed
*/
static int collect_data(tcp_stream *s, myevent_s *ev)
{
    if (s == NULL) return -1;
    s->data_recved += buf_data_size(s->out_buf);
    s->reply_body_size_left -= buf_data_size(s->out_buf);
    buf_put(s->work_buf, s->out_buf->buffer, buf_data_size(s->out_buf));
    clear_space(s->out_buf);
    if (s->reply_body_size_left == 0) {
        if (s->data_recved != s->content_len) {
            clear_space(s->out_buf);
            close_stream(g_efd, ev, 1);
            return -1;
        } else {
            return 1;
        }
    }
    return 0;
}

static void alloc_work_buff(tcp_stream *s)
{
    s->work_buf = mem_alloc("work_buf", sizeof(buf_t));
    memset(s->work_buf, 0x0, sizeof(buf_t));
    s->work_buf->buffer = mem_alloc("work_buf_data", LOG_MAX_LEN);
    s->work_buf->buffer[0] = 0;
    s->work_buf->data_len = 0;
    s->work_buf->buf_len = LOG_MAX_LEN;
    return;
}

static void timeout_callback(int fd, void *data)
{
    //close TODO
}

static void data_transfer(int sock, int read_event, int write_event, void *data) {
    myevent_s * ev = (myevent_s*)data;
    tcp_stream* s = ev->stream;
    assert(sock == s->server_sock);
    if(read_event == 0 && write_event == 0) {
        close_stream(g_efd, ev, 2);
        return;
    }
    int size = 0;

    if(write_event) {
        if(stream_flush_out(s, -1) < 0) {
            close_stream(g_efd, ev, 4);
            return;
        }
    }

    if(read_event) {
        assert(buf_data_size(s->out_buf) == 0);
        if((stream_feed_out(s)) < 0) {
            if(buf_data_size(s->out_buf) == 0) {
                close_stream(g_efd, ev, 3);
                return;
            }

            if(buf_data_size(s->out_buf) > 0) {
                int ret = collect_data(s, ev);
                if (ret == 1) {
                    do_work(s->work_buf->buffer, s->data_recved);
                    g_qps++;
                    close_stream(g_efd, ev, 1);
                    return;
                } else if (ret == -1) {
                    return;
                }
            }
        }
        if(s->out_buf->data_len < s->out_buf->buf_len) {
            char* enddata = s->out_buf->buffer + s->out_buf->data_len;
            *enddata = 0;
        }
        if(s->reply_body_size_left > 0) {
            int ret = collect_data(s, ev);
            if (ret == 1) {
                do_work(s->work_buf->buffer, s->data_recved);
                g_qps++;
                close_stream(g_efd, ev, 1);
                return;
            } else if (ret == -1) {
                return;
            }
        } else if(s->reply_body_size_left == 0) {
            if (buf_data_size(s->out_buf) < sizeof(uint64_t)) {
                goto cont;
            }

            if (*((uint64_t*)s->out_buf->buffer) > LOG_MAX_LEN ||
                *((uint64_t*)s->out_buf->buffer) == 0) {
                clear_space(s->out_buf);
                close_stream(g_efd, ev, 1);
                return;
            }

            s->reply_body_size_left = *((uint64_t*)s->out_buf->buffer);
            s->content_len = *((uint64_t*)s->out_buf->buffer);
            buf_pick(s->out_buf, sizeof(uint64_t));
            alloc_work_buff(s);

            if(s->reply_body_size_left > 0) {
                int ret = collect_data(s, ev);
                if (ret == 1) {
                    do_work(s->work_buf->buffer, s->data_recved);
                    g_qps++;
                    close_stream(g_efd, ev, 1);
                    return;
                } else if (ret == -1) {
                    return;
                }
            }
        }
    }
    cont:
    set_event_in_out(g_efd, ev);
    //event_set_timeout(ev, 0, timeout_callback);
}

