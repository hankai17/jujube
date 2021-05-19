#ifndef _CONNECT_H
#define _CONNECT_H
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#include "list.h"
#include "ip.h"
#include "comm.h"

#define BEGIN_CONN 0
#define CONN_ESTAB 1
#define CONN_CLOSE 2

typedef struct connect_pool {

    uint32_t max_connects;
    uint32_t max_alive_times;
    uint32_t max_txn;
    uint32_t current_total; // atomic TODO
} connect_pool;

typedef struct jconnect {
    struct list_head list;

    jujube_in_addr jaddr; 
    uint32_t port; 
    int fd;
    int is_alive;
    uint32_t create_time;
    uint32_t requests;
    int status;
    struct tcp_stream *stream;    
} jconnect;

void init_connect_pool();
jconnect* get_connection();
void release_connection(jconnect* jc);
void release_keepalive_connection(jconnect *jc);

#endif

