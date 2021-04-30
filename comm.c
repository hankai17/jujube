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

int set_wake_up_polling(int efd, int tickle_fd[2])
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
    if (fcntl(tickle_fd[0], F_SETFD, FD_CLOEXEC)) {
        return -1;
    }
    if (fcntl(tickle_fd[1], F_SETFL, O_NONBLOCK)) {
        return -1;
    }
    if (fcntl(tickle_fd[1], F_SETFD, FD_CLOEXEC)) {
        return -1;
    }
    if (epoll_ctl(efd, EPOLL_CTL_ADD, tickle_fd[0], &ev)) {
        return -1;
    }
    return 0;
}

void wake_up_poll(int tickle_fd[2])
{
    int ret = write(tickle_fd[1], "M", 1);
    // check ret TODO
}

int create_connect_socket(struct sockaddr_storage *peeraddr, 
        jujube_in_addr jaddr, int ip_flag, int port) 
{
    int sock = -1;
    int r = 0;
    if (ip_flag == USE_IPV4) {
        struct sockaddr_in *peer_ipv4_addr = (struct sockaddr_in *)peeraddr;
        peer_ipv4_addr->sin_family = AF_INET;
        peer_ipv4_addr->sin_port = port;
        memcpy(&peer_ipv4_addr->sin_addr, &jaddr.inx_addr.ipv4_addr,
                sizeof(struct sockaddr_in));
        sock = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        struct sockaddr_in6 *peer_ipv6_addr = (struct sockaddr_in6 *)peeraddr;
        peer_ipv6_addr->sin6_family = AF_INET6;
        peer_ipv6_addr->sin6_port = port;
        memcpy(&peer_ipv6_addr->sin6_addr, &jaddr.inx_addr.ipv6_addr,
                sizeof(struct sockaddr_in6));
        sock = socket(AF_INET6, SOCK_STREAM, 0);
    }
    if(sock < 0) {
        return -1;
    }
    /*
    struct linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&l, sizeof(l));
    */

    if(comm_set_nonblock(sock) < 0) {
        return -2;
    }
    r = connect_nonb(sock, (struct sockaddr *)peeraddr);
    if(r == -1) {
        return -3;
    }
    return sock; 
}


