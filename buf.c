#include "buf.h"
#include "mem.h"

#include <assert.h>
#include <string.h>

size_t  buf_free_space(buf_t * b)
{
	return b->buf_len - b->data_len;
}

size_t clear_space(buf_t * b)
{
	b->buffer[0] = '\0';
	b->data_len = 0;
	return 0;
}

size_t buf_data_size(buf_t *b)
{
	return b->data_len;
}


char * buf_end_position(buf_t *b)
{
	return b->buffer + b->data_len;
}

int buf_pick(buf_t *b, size_t len)
{
	assert(b->data_len >= len);
	b->data_len -= len;
	if(b->data_len > 0){
		memmove(b->buffer, b->buffer+len, b->data_len);
	}
	return 0;
}

int buf_realloc( buf_t * b, size_t len)
{
	char * n;
	n = (char *)mem_alloc("buf",  b->buf_len+len);
	memcpy( n, b->buffer, b->data_len );
	mem_free((void *)b->buffer);
	b->buffer = n;
	b->buf_len += len;
	return 0;
}

int buf_put(buf_t *b, char *data, size_t len)
{
	if(buf_free_space(b) < len ) {
		/* no recommended to do this, so give some warning here */
		if(buf_realloc(b, len) < 0)
			return -1;
	}
	memcpy(b->buffer+b->data_len, data, len);
	b->data_len += len;
	return 0;
}

