#include "comm.h"
#include "fastlz.h"
#include <unistd.h>

static myevent_s g_events[MAX_EVENTS + 1];
static int g_efd;
int tickle_fd[2] = {0};

void main_loop();
static int create_transaction(struct transfer_log_entry* log_entry);
static void connect_cb(int sock, int read_event, int write_event, void *data);
static void keepalive_cb(int sock, int read_event, int write_event, void *data);
static void data_transfer(int sock, int read_event, int write_event, void *data);

extern struct transfer_log_entry* get_log_entry_from_list();

static void attach_stream_to_connect(struct jconnect *jc, tcp_stream *s) 
{
    printf("attach_stream_to_connect\n");
    jc->stream = s;
    struct myevent_s* ev;

    // reset connection event && cb
    if (jc->status == BEGIN_CONN) { // a new connection
        ev = evget(g_events, -1);
        eventset(ev, jc, connect_cb, ev);
    } else if (jc->status == CONN_ESTAB) { // a keep-alive connection
        ev = evget(g_events, jc->fd);
        //printf("ev.status: %d\n", ev->status);
        if (1) {
            eventdel(g_efd, ev); // Make sure that EPOLLOUT is add success
            eventset(ev, jc, data_transfer, ev);
        } else {
            event_reset_cb(ev, data_transfer, ev);
        }
    }
    eventadd(g_efd, EPOLLOUT, ev);
    // TODO
    return;
}

static void release_stream_from_connect(myevent_s* ev, int is_bad_conn) 
{
    printf("release_stream_from_connect, is_bad: %d\n", is_bad_conn);
    struct jconnect *jc = ev->jc;
    tcp_stream *s = jc->stream;

    free_transaction_stream(s); 
    jc->stream = NULL;

    // reset connection event
    if (is_bad_conn) {
        ev->jc->is_alive = 0;
        ev->jc->status = CONN_CLOSE;
        eventdel(g_efd, ev);
    } else {
        //eventset(ev, ev->jc, keepalive_cb, ev);
        //eventadd(g_efd, EPOLLIN | EPOLLOUT, ev);  // Because write always trigger SO we do not care about it
        event_reset_cb(ev, keepalive_cb, ev);
        eventadd(g_efd, EPOLLIN, ev); 
    }
    release_connection(jc);
}

static void connect_cb(int sock, int read_event, 
        int write_event, void *data)
{
    myevent_s *ev = (myevent_s*)data;
    struct jconnect *jc = ev->jc;
    tcp_stream *s = jc->stream;
    assert(sock == s->server_sock);
    int ret = 0;
    //if (read_event == 1 || write_event == 0) {
    if (write_event == 0) {
        printf("connect failed\n");
        release_stream_from_connect(ev,  BAD_CONNECT);
        return;
    }

    // Do not judge firstly. We should write & get write's ret
    //if(getsockopt(s->server_sock, SOL_SOCKET,SO_ERROR,&error,&n) < 0) {
    //    return;
    //}

    if (write_event) {
        eventset(ev, jc, data_transfer, ev);
        eventadd(g_efd, EPOLLOUT, ev);
    }
}

static void keepalive_cb(int sock, int read_event, 
        int write_event, void *data) {
    printf("keepalive cb, read_event: %d, write_event: %d\n",
            read_event, write_event);

    int error;
    int n;
    myevent_s *ev = (myevent_s*)data;
    struct jconnect *jc = ev->jc;
    tcp_stream *s = jc->stream;

    if(getsockopt(sock, SOL_SOCKET,SO_ERROR, &error,&n) < 0) {
        printf("keeplive read err\n");    
    }

    jc->is_alive = 0;
    eventdel(g_efd, ev);
    release_keepalive_connection(jc); // Why list_del?
}

static int create_transaction(struct transfer_log_entry* log_entry) 
{
    int r = 0;
    int sock = 0;
    char prefix_len[8] = {0};

    char fast_compress_buff[32] = {0};
    uint64_t compress_ret = fastlz_compress_level(2, log_entry->gather_logmsg, log_entry->msg_len, fast_compress_buff);
    memcpy(prefix_len, &compress_ret, sizeof(uint64_t));

    struct sockaddr_storage peeraddr;
    jujube_in_addr jaddr;
    memset(&peeraddr, 0x0, sizeof(struct sockaddr_storage));
    memcpy(&jaddr, &log_entry->ip, sizeof(struct jujube_in_addr));

    struct jconnect* jc = get_connection();
    if (jc == NULL) {
        printf("not hit, will create a new conn\n");
        int size = sizeof(struct jconnect);
        jc = (jconnect*)malloc(size);
        if (jc == NULL) {
            goto end;
        }
        sock = create_connect_socket(&peeraddr, jaddr, log_entry->ip.flag, log_entry->port);
        if (sock < 0) {
            goto end;
        }
        jc->fd = sock;
        jc->is_alive = 0;
        jc->status = BEGIN_CONN;
    } else {
        printf("hit!!!!!!!!!!!!!!!!!!!!!!\n");
    }
    tcp_stream *s = new_transaction_stream(&peeraddr, jc->fd, prefix_len, fast_compress_buff, compress_ret);
    attach_stream_to_connect(jc, s);

end:
    if (log_entry) {
        free(log_entry);
    }
    return 0;
}

void main_loop() {
    mem_module_init();
    g_efd = epoll_create(MAX_EVENTS + 1);
    if(g_efd <= 0) {
        return;
    }
    struct epoll_event events[MAX_EVENTS + 1];
    int checkpos = 0, i;
    signal(SIGPIPE, SIG_IGN);
    if (set_wake_up_polling(g_efd, tickle_fd)) {
        return;
    }
    int next_time = 0;

    while(1) {
        long now = time(NULL);
        /*
        struct transfer_log_entry* log_entry = NULL; //get_log_entry_from_list();
        if (log_entry) {
            create_transaction(log_entry); // ret TODO
        }
        */

        if (now >= next_time) {
            struct transfer_log_entry* log_entry = 
                    (struct transfer_log_entry*)malloc(sizeof(struct transfer_log_entry));
            char msg[64] = "1234567890";   
            char ip_buf[64] = "192.168.104.159";
            check_host_ip_and_get_verion(ip_buf, &log_entry->ip);
            log_entry->proto = 1;
            log_entry->port = htons(9527);
            log_entry->msg_len = strlen(msg);
            memcpy(log_entry->gather_logmsg, msg, strlen(msg)); 
            
            create_transaction(log_entry);
            next_time = now + 1;
        }

        expires_house_keeping(g_efd, &checkpos, g_events, now, tickle_fd[0]);
        int nfd = epoll_wait(g_efd, events, MAX_EVENTS + 1, 1000);
        if (nfd < 0 && errno != EINTR) {
            break;
        }
        for (i = 0; i < nfd; i++) {
            if (events[i].data.fd == tickle_fd[0]) {
                uint8_t goddess;
                while (read(tickle_fd[0], &goddess, 1) == 1);
                continue;
            }
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;
            ev->call_back(ev->fd, events[i].events & EPOLLIN ? 1 : 0, 
                    events[i].events & EPOLLOUT ? 1 : 0, ev->arg);
        }
    }
    close(g_efd);
    return;
}

static void data_transfer(int sock, int read_event, int write_event, void *data) {
    //printf("socket: %d, data_transfer read_event: %d, write_event: %d\n", 
    //        sock, read_event, write_event);
    myevent_s *ev = (myevent_s*)data;
    tcp_stream *s = ev->jc->stream;
    assert(sock == s->server_sock);
    if (read_event == 0 && write_event == 0) {
        release_stream_from_connect(ev,  BAD_CONNECT);
        return;
    }
    int ret = 0;

    if (write_event) {
        printf("connected ok\n");
        if((ret = stream_flush_out(s, -1)) < 0) {
            release_stream_from_connect(ev,  BAD_CONNECT);
            return;
        }

        ev->jc->is_alive = 1;
        ev->jc->status = CONN_ESTAB;

        if (buf_data_size(s->in_buf) == 0) {
            printf("send data done\n");
            release_stream_from_connect(ev,  OK_CONNECT);
            return;
        }
    }

    if (read_event) {
        if((stream_feed_out(s)) < 0) {
            release_stream_from_connect(ev,  BAD_CONNECT);
        } else {
            release_stream_from_connect(ev,  BAD_CONNECT);
        }
        return;
    }

    set_stream_event_in_out(g_efd, ev);
}

int main()
{
    init_connect_pool();
    main_loop();
    return 0;
}

// ev-->jc--->stream
// 1) 如何确保 inactivate时 ev一直被占着
