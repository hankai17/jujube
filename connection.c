#include "connection.h"

static connect_pool g_connect_pool;
static struct list_head g_jconnect;
static pthread_mutex_t g_conn_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

void init_connect_pool() {
    g_connect_pool.max_connects = 10;
    g_connect_pool.max_alive_times = 60;
    g_connect_pool.max_txn = 100;
    g_connect_pool.current_total = 0;
    INIT_LIST_HEAD(&g_jconnect);
}

void get_connection_statiscs() {
    struct list_head *cursor = NULL;
    int i = 0;

    pthread_mutex_lock(&g_conn_pool_mutex);
    list_for_each(cursor, &g_jconnect) {
        i++;
    }
    pthread_mutex_unlock(&g_conn_pool_mutex);
    printf("pool current_total: %d\n", i);
}

jconnect* get_connection() {
    struct list_head *cursor = NULL;
    struct list_head *next_cursor = NULL;
    struct jconnect *tmp_conn = NULL;
    struct jconnect *curr_conn = NULL;
    struct list_head invaild_conn;

    INIT_LIST_HEAD(&invaild_conn);

    pthread_mutex_lock(&g_conn_pool_mutex);
    for (cursor = (&g_jconnect)->next; cursor != &g_jconnect;) {
        tmp_conn = (jconnect*)cursor;
        next_cursor = cursor->next;
        
        list_del(&tmp_conn->list);
        if (!tmp_conn->is_alive) {
            list_add_tail(&tmp_conn->list, &invaild_conn); 
            cursor = next_cursor;
            continue;
        }
        // OK
        curr_conn = tmp_conn;
        break;
    }
    pthread_mutex_unlock(&g_conn_pool_mutex);

    for (cursor = (&invaild_conn)->next; cursor != &invaild_conn;) {
        next_cursor = cursor->next;
        tmp_conn = (struct jconnect*)cursor;
        close(tmp_conn->fd);
        free(tmp_conn);
        g_connect_pool.current_total--;
        cursor = next_cursor;
    }
    return curr_conn;    
}

void release_connection(jconnect* jc) {
    jc->requests++;
    //if (g_connect_pool.current_total >= g_connect_pool.max_connects ||
            if (!jc->is_alive) { // ||
            //jc->requests >= g_connect_pool.max_txn ||
            //jc->create_time + g_connect_pool.max_alive_times >= time(NULL)) {
        close(jc->fd);
        free(jc);
        --g_connect_pool.current_total;
        return;
    }
    pthread_mutex_lock(&g_conn_pool_mutex);
    ++g_connect_pool.current_total;
    list_add_tail(&jc->list, &g_jconnect);
    pthread_mutex_unlock(&g_conn_pool_mutex);
}

void release_keepalive_connection(jconnect *jc) {
    close(jc->fd);
    list_del(&jc->list);
    free(jc);
    --g_connect_pool.current_total;
    return;
}

