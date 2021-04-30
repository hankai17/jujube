#ifndef _EVENT_H
#define _EVENT_H

#include "comm.h"

typedef void timeout_handler_t(int, void *);
typedef struct myevent_s {
    int fd;
    int events;
    void *arg;
    void (*call_back)(int fd, int read_event, int write_event, void *arg);
    int status;
    long last_active;
    timeout_handler_t *thandler;
    struct jconnect *jc;
} myevent_s;

myevent_s* evget(myevent_s *g_events, int fd/*is_create*/);
void eventset(struct myevent_s *ev, struct jconnect *jc,
              void (*call_back)(int, int, int, void *), void *arg);
void event_reset_cb(struct myevent_s *ev, 
        void (*call_back)(int, int, int, void *), void *arg);
void eventadd(int efd, int events, struct myevent_s *ev);
void eventdel(int efd, struct myevent_s *ev);
void set_stream_event_in_out(int efd, myevent_s *ev);

void close_stream(int g_efd, myevent_s* ev, int reason);

void expires_house_keeping(int g_efd, int *pos, myevent_s *g_events, long now, int except_fd);
void init_listensocket(int efd, short port, myevent_s *g_events,
                       jujube_in_addr *jaddr, void (*call_back)(int, int, int, void *));

#endif

