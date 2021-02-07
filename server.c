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

int sys_start;
#define cfg_http_buf_size (8*1024) 
#define mid_http_buf_size (64*1024) 
#define MAX_EVENTS  1024

typedef struct tcp_stream {
	time_t data_start;
	int64_t data_in;
	int64_t data_out;
	long	data_begin;
	int client_sock;
	int server_sock;

	#ifdef USE_IPV6
	int client_is_ipv6;
	struct sockaddr_in6 client_addr;
	struct sockaddr_in server_addr;
	struct sockaddr_in6 server_addr_ipv6;
	#else
	struct sockaddr_in client_addr;
	struct sockaddr_in server_addr;
	#endif
	buf_t  * in_buf;
	buf_t  * out_buf;
	int64_t reply_body_size_left;

	struct {		
		unsigned int request_continue:1; /*request not complete to parse*/
		unsigned int request_rewrite:1;
		unsigned int eof:1;  /*last block of reply data in a request */
		unsigned int server_closed:1; /*when server closed, send the data in out_buf,and close stream */
		/*status necessary to http_set_event_in */
		unsigned int fill_reply: 1; 
		unsigned int fill_reply_body: 1;
		unsigned int usedomaincache: 1;
		unsigned int  count_in: 1;
		unsigned int  count_out: 1;
		//unsigned int client_close: 1;
		unsigned int need_close_connect: 1;
		unsigned int  focusin_flow_ip: 1;
	} flags;

} tcp_stream;

typedef struct myevent_s {
    int fd;                 
    int events;             //EPOLLIN  EPLLOUT
    void *arg;              
    void (*call_back)(int fd, int read_event, int write_event, void *arg);
    int status; 	    //1 means polling; 0 means not polling
    long last_active;
    tcp_stream *stream;
} myevent_s;

myevent_s g_events[MAX_EVENTS + 1];
static int g_efd;

void* main_loop(void* arg);
static tcp_stream* stream_create(int sock, struct sockaddr_in* paddr);
static int create_socket(unsigned long lbe_ip, unsigned short lbe_port);
static int stream_flush_out(tcp_stream *s, int size);
static int stream_feed_out(tcp_stream *s);
static void data_transfer(int sock, int read_event, int write_event, void *data);
static void download_set_event_in_out(myevent_s *ev);
static void close_stream(myevent_s* ev, int reason);

static myevent_s* evget();
static void eventset(struct myevent_s *ev, int fd, tcp_stream* si, void (*call_back)(int, int, int, void *), void *arg);
static void eventadd(int efd, int events, struct myevent_s *ev);
static void eventdel(int efd, struct myevent_s *ev);

static long next_run_time;

const char * xstrerror(void)
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
		printf("FD %d: fcntl F_GETFL: %s\n", fd, xstrerror());	    
		return -1;
	}
	
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		printf("commSetNonBlocking: FD %d: %s\n", fd, xstrerror()); 
		return -1;
	}

	return 0;
}

void accept_conn(int lfd, int read_events, int write_event, void *arg)
{
    struct sockaddr_in cin;
    socklen_t len = sizeof(cin);
    int cfd, i;

    if ((cfd = accept(lfd, (struct sockaddr *)&cin, &len)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
        }
        printf("%s: accept, %s\n", __func__, strerror(errno));
        return;
    }

    do {
        for (i = 0; i < MAX_EVENTS; i++) {
            if (g_events[i].status == 0)
                break;
        }

        if (i == MAX_EVENTS) {
            printf("%s: max connect limit[%d]\n", __func__, MAX_EVENTS);
            break;
        }

        int flag = 0;
        if ((flag = fcntl(cfd, F_SETFL, O_NONBLOCK)) < 0)
        {
            printf("%s: fcntl nonblocking failed, %s\n", __func__, strerror(errno));
            break;
        }

         tcp_stream* s = stream_create(cfd, (struct sockaddr_in*)&cin);
         s->flags.server_closed = 0;

        struct myevent_s* ev = evget();
        eventset(ev, cfd, s, data_transfer, ev);
        eventadd(g_efd, EPOLLIN, ev);

    } while(0);

    printf("new connect [%s:%d][time:%ld], pos[%d]\n", inet_ntoa(cin.sin_addr), ntohs(cin.sin_port), g_events[i].last_active, i);
    return;
}

void init_listensocket(int efd, short port)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(lfd, F_SETFL, O_NONBLOCK);

    eventset(&g_events[MAX_EVENTS], lfd, NULL, accept_conn, &g_events[MAX_EVENTS]);
    //g_events[MAX_EVENTS];

    eventadd(efd, EPOLLIN, &g_events[MAX_EVENTS]);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    bind(lfd, (struct sockaddr *)&sin, sizeof(sin));
    listen(lfd, 128);
    return;
}

int init_main_loop() {
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, main_loop, NULL);
    return tid;
}

void* main_loop(void* arg) {
    //pthread_setname_np(pthread_self(), "server_main_loop");
    g_efd = epoll_create(MAX_EVENTS + 1);
    if(g_efd <= 0) {
        printf("create efd err %s\n", strerror(errno));
        return 0;
    }
    
    int port = 9527;
    init_listensocket(g_efd, port);

    struct epoll_event events[MAX_EVENTS + 1];
    int checkpos = 0, i;
    next_run_time = time(NULL);
    signal(SIGPIPE, SIG_IGN);

    while(1) {
        long now = time(NULL);
        while (sys_start != 1) {
            sleep(1);
        }

        for (i = 0; i < 100; i++, checkpos++) {
            if (checkpos == MAX_EVENTS)
                checkpos = 0;
            if (g_events[checkpos].status != 1)
                continue;
            long duration = now - g_events[checkpos].last_active;
            if (duration >= 10) {
                printf("siteinfo_download fd: %d, timeout\n", g_events[checkpos].fd);
                eventdel(g_efd, &g_events[checkpos]);
                close_stream(&g_events[checkpos], 0);
            }
        }
        int nfd = epoll_wait(g_efd, events, MAX_EVENTS + 1, 1000);
        if (nfd < 0 && errno != EINTR) {
            printf("epoll_wait err %s, errno: %d\n", strerror(errno), errno);
            break;
        }

        printf("debug...epoll_wait ret: %d\n", nfd);
        for (i = 0; i < nfd; i++) {
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;
            ev->call_back(ev->fd, events[i].events & EPOLLIN ? 1 : 0, events[i].events & EPOLLOUT ? 1 : 0, ev->arg);
        }
    }
    printf("thread epoll_wait err %s\n", strerror(errno));
    close(g_efd);
    return 0;
}

static myevent_s* evget() {
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

static void eventset(struct myevent_s *ev, int fd, tcp_stream* s, void (*call_back)(int, int, int, void *), void *arg) {
    ev->fd = fd;
    ev->call_back = call_back;
    ev->events = 0;
    ev->arg = arg;
    ev->status = 0;
    ev->last_active = time(NULL);
    ev->stream = s;
    return;
}

static void eventadd(int efd, int events, struct myevent_s *ev) {
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
        printf("eventadd failed fd: %d, events: %d, op: %d, err: %s\n", ev->fd, events, op,  xstrerror());
    } else {
        ;
        printf("eventadd OK [fd=%d], op=%d, events[%0X]\n", ev->fd, op, events);
    }

    return;
}

static void eventdel(int efd, struct myevent_s *ev) {
    struct epoll_event epv = {0, {0}};

    if (ev->status != 1)
        return;

    epv.data.ptr = ev;
    ev->status = 0;
    if(epoll_ctl(efd, EPOLL_CTL_DEL, ev->fd, &epv) < 0) {
        printf("eventdell fd: %d, err: %s\n", ev->fd, xstrerror());
    } else {
        ;
        printf("eventdell fd: %d, ok\n", ev->fd);
    }

    return;
}

static tcp_stream* stream_create(int sock, struct sockaddr_in* paddr) {
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
    memcpy(&s->client_addr, paddr, sizeof(struct sockaddr_in));

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

static void format_request(char* buf, int len, unsigned long lbe_ip, unsigned short lbe_port) {
    return;
}

int create_socket(unsigned long lbe_ip, unsigned short lbe_port) {
    int r = 0;
    int sock = 0;
    struct sockaddr_in peeraddr;
    char siteinfo_req[1024] = {0};

    format_request(siteinfo_req, sizeof(siteinfo_req) - 1, lbe_ip, lbe_port);

    memset(&peeraddr, 0, sizeof(peeraddr));
    peeraddr.sin_family = AF_INET;
    peeraddr.sin_addr.s_addr = lbe_ip;	
    peeraddr.sin_port = htons(lbe_port);

    sock = socket(AF_INET,SOCK_STREAM,0);
    if(sock < 0) {
        printf("create_socket: socket failed: %s\n", xstrerror());
        return 0;
    }
    struct linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&l, sizeof(l));

    tcp_stream* s = stream_create(sock, &peeraddr);
    s->flags.server_closed = 0;

    struct myevent_s* ev = evget();
    eventset(ev, sock, s, data_transfer, ev);
    eventadd(g_efd, EPOLLOUT, ev);

    buf_put(s->in_buf, siteinfo_req, strlen(siteinfo_req));

    if(comm_set_nonblock(sock) < 0) {
        printf("create_socket: comm_set_nonblock failed: %s\n", xstrerror());
        close_stream(ev, 2);
        return 0;
    }
    r = connect_nonb(sock, (struct sockaddr *)&peeraddr);
    if(r == -1) {
        printf("create_socket: connect failed: %s\n", xstrerror());
        close_stream(ev,  3);
        return 0;
    }
    printf("create_socket to transfer data\n");
    return 0;
}

static int stream_flush_out(tcp_stream *s, int size) {
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

static int stream_feed_out(tcp_stream *s) {
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

static void data_transfer(int sock, int read_event, int write_event, void *data) {
    myevent_s * ev = (myevent_s*)data;
    tcp_stream* s = ev->stream;
    assert(sock == s->server_sock);
    if(read_event == 0 && write_event == 0) {
        printf("data_transfer: read & write event are 0\n");
        close_stream(ev, 2);
        return;
    }
    int size = 0;

    if(write_event) { //send req
        if(stream_flush_out(s, -1) < 0) {
            printf("data_transfer: lbe close when send req\n");
            close_stream(ev, 4);
            return;
        }
    }

    if(read_event) { //recv resp
        assert(buf_data_size(s->out_buf) == 0);
        if((stream_feed_out(s)) < 0) { //peer closed or other err
            if(buf_data_size(s->out_buf) == 0) {
                printf("data_transfer: lbe closed and buf data size is 0\n");
                close_stream(ev, 3);
                return;
            }
            /*
               if(buf_data_size(s->out_buf) > 0) {
               NEED_DEUBG_MODULE(DEBUG_LV_1,DEBUG_FREE)printf("data_transfer: lbe closed and exists buf data\n");
               }
               */
        }
        if(s->out_buf->data_len < s->out_buf->buf_len) {
            char* enddata = s->out_buf->buffer + s->out_buf->data_len;
            *enddata = 0;
        }
        if(s->reply_body_size_left > 0) {
            size = buf_data_size(s->out_buf);
            s->reply_body_size_left -= size;
            clear_space(s->out_buf);
            if(s->reply_body_size_left == 0 || s->flags.server_closed) {
                close_stream(ev, 1);
            }
        } else if(s->reply_body_size_left == 0) {
            if (buf_data_size(s->out_buf) < sizeof(uint64_t)) {
                goto cont;
            }
            s->reply_body_size_left = *((uint64_t*)s->out_buf->buffer);
            buf_pick(s->out_buf, sizeof(uint64_t));
            printf("data_transfer start, sum_size: %ld\n", s->reply_body_size_left);

            if(s->reply_body_size_left > 0) {
                s->reply_body_size_left -= buf_data_size(s->out_buf);
                clear_space(s->out_buf);
            }
            if(s->reply_body_size_left == 0) {
                close_stream(ev, 1);
                return;
            }
            /*
               if(s->flags.server_closed == 1) {
               printf("data_transfer: server closed\n");
               close_stream(ev, 0);
               return;
               }
               */
        }
    }
cont:
    download_set_event_in_out(ev);
}

static void download_set_event_in_out(myevent_s *ev) {
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

static void close_stream(myevent_s* ev, int reason) {
    if(ev != NULL) {
        tcp_stream* s = ev->stream;
        printf("siteinfo_download close_stream, s->reply_body_size_left: %ld\n", 
              s->reply_body_size_left);
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
        mem_free(s);
    }
    return;
}

int main()
{
    mem_module_init();
    init_main_loop();
    // do other init
    sys_start = 1;
    while(1) {
        sleep(1);
    }
}

