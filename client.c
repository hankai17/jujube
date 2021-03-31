#include "comm.h"
#include "fastlz.h"
#include <unistd.h>

static myevent_s g_events[MAX_EVENTS + 1];
static int g_efd;
int tickle_fd[2] = {0};

void main_loop();
static int create_socket(struct transfer_log_entry* log_entry);
static void data_transfer(int sock, int read_event, int write_event, void *data);
extern struct transfer_log_entry* get_log_entry_from_list();

static int set_wake_up_polling()
{
    int ret = 0;
    if (pipe(tickle_fd)) {
        return -1;
    }
    struct epoll_event ev;
    memset(&ev, 0x0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.fd = tickle_fd[0];
    if (fcntl(tickle_fd[0], F_SETFL, O_NONBLOCK)) {
        return -1;
    }
    //if (fcntl(tickle_fd[1], F_SETFL, O_NONBLOCK)) { // TODO
    if (epoll_ctl(g_efd, EPOLL_CTL_ADD, tickle_fd[0], &ev)) {
        return -1;
    }
    return 0;
}

void wake_up_poll()
{
    int ret = write(tickle_fd[1], "M", 1);
    // check ret TODO
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
    if (set_wake_up_polling()) {
        return;
    }

    while(1) {
        long now = time(NULL);
        struct transfer_log_entry* log_entry = get_log_entry_from_list();
        if (log_entry) {
            create_socket(log_entry); // ret TODO
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
            ev->call_back(ev->fd, events[i].events & EPOLLIN ? 1 : 0, events[i].events & EPOLLOUT ? 1 : 0, ev->arg);
        }
    }
    close(g_efd);
    return;
}

static int create_socket(struct transfer_log_entry* log_entry) {
    int r = 0;
    int sock = 0;
    char msg_info[32 * 1024] = {0};
    char prefix_len[8] = {0};

#if USE_FASTLZ
    char fast_compress_buff[34407] = {0};
    //uint64_t compress_ret = fastlz_compress(log_entry->gather_logmsg, log_entry->msg_len, fast_compress_buff);
    uint64_t compress_ret = fastlz_compress_level(2, log_entry->gather_logmsg, log_entry->msg_len, fast_compress_buff);
    //if (compress_ret < 0) { } // TODO
    memcpy(prefix_len, &compress_ret, sizeof(uint64_t));
#else
    memcpy(prefix_len, &log_entry->msg_len, sizeof(log_entry->msg_len));
    memcpy(msg_info, log_entry->gather_logmsg, log_entry->msg_len);
#endif

    struct sockaddr_storage peeraddr;
    jujube_in_addr jaddr;
    memset(&peeraddr, 0x0, sizeof(struct sockaddr_storage));
    memcpy(&jaddr, &log_entry->ip, sizeof(struct jujube_in_addr));

    if (log_entry->ip.flag == USE_IPV4) {
        struct sockaddr_in *peer_ipv4_addr = (struct sockaddr_in *)&peeraddr;
        peer_ipv4_addr->sin_family = AF_INET;
        peer_ipv4_addr->sin_port = (log_entry->port);
        memcpy(&peer_ipv4_addr->sin_addr, &jaddr.inx_addr.ipv4_addr,
               sizeof(struct sockaddr_in));
        sock = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        struct sockaddr_in6 *peer_ipv6_addr = (struct sockaddr_in6 *)&peeraddr;
        peer_ipv6_addr->sin6_family = AF_INET6;
        peer_ipv6_addr->sin6_port = (log_entry->port);
        memcpy(&peer_ipv6_addr->sin6_addr, &jaddr.inx_addr.ipv6_addr,
               sizeof(struct sockaddr_in6));
        sock = socket(AF_INET6, SOCK_STREAM, 0);
    }

    if(sock < 0) {
        goto end;
    }
    struct linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&l, sizeof(l));

    tcp_stream* s = stream_create(sock, &peeraddr);
    s->flags.server_closed = 0;

    struct myevent_s* ev = evget(g_events);
    eventset(ev, sock, s, data_transfer, ev);
    eventadd(g_efd, EPOLLOUT, ev);

    buf_put(s->in_buf, prefix_len, 8);
#if USE_FASTLZ
    buf_put(s->in_buf, fast_compress_buff, compress_ret);
#else
    buf_put(s->in_buf, msg_info, log_entry->msg_len);
#endif

    if(comm_set_nonblock(sock) < 0) {
        close_stream(g_efd, ev, 2);
        goto end;
    }
    r = connect_nonb(sock, (struct sockaddr *)&peeraddr);
    if(r == -1) {
        close_stream(g_efd, ev,  3);
        goto end;
    }
    end:
    if (log_entry) {
        free(log_entry);
    }
    return 0;
}

static void data_transfer(int sock, int read_event, int write_event, void *data) {
    myevent_s *ev = (myevent_s*)data;
    tcp_stream *s = ev->stream;
    assert(sock == s->server_sock);
    if (read_event == 0 && write_event == 0) {
        close_stream(g_efd, ev, 2);
        return;
    }
    int ret = 0;

    if (write_event) {
        if((ret = stream_flush_out(s, -1)) < 0) {
            close_stream(g_efd, ev, 4);
            return;
        }
        if (buf_data_size(s->in_buf) == 0) {
            close_stream(g_efd, ev, 0);
            return;
        }
    }

    if (read_event) {
        if((stream_feed_out(s)) < 0) {
            close_stream(g_efd, ev, 3);
        } else {
            close_stream(g_efd, ev, 3); // minion do not recv data
        }
    }

    set_event_in_out(g_efd, ev);
}

