#ifndef BUF_H
#define BUF_H

#include <sys/types.h>
#include "list.h"

typedef struct buf_s {
	char *buffer;
	size_t buf_len;
	size_t data_len;
} buf_t;

typedef struct {
	struct list_head dlist;
	buf_t * buf;
} buf_chain_t;

size_t  buf_free_space(buf_t * b);
size_t clear_space(buf_t * b);
size_t buf_data_size(buf_t *b);
char * buf_end_position(buf_t *b);
int buf_pick(buf_t *b, size_t len);
int buf_put(buf_t *b, char * data, size_t len);

#endif
