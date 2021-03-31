#ifndef _MEM_H
#define _MEM_H

#include "list.h"
#include <sys/types.h>

#define MAX_MEM_BLOCK (64*1024)
#define MAX_EXT_MEM_BLOCK (1024*1024)

//a mem pool is a double list of memblock of same size
typedef struct  {
	struct list_head dlist;
	size_t obj_size;
	int refcount;
#ifdef DEBUG_MEM_CHECK
	const char *module;
#endif
	char obj[1];
} mem_entry_t;

typedef struct {
	int blocks_used;
	int blocks_free;
} mem_stat_t;

void  mem_module_init();
void *  mem_alloc(const char *module, size_t size);
void  mem_free(void *buf);
void *  mem_realloc(void *buf, size_t old_size ,size_t new_size);
extern mem_stat_t mem_stat[];

#ifdef DEBUG_MEM_CHECK
int64_t mem_check_mod_count( const char *module , int *objsize);
#endif

int statistics_mem_info(char buff[], size_t buff_sz);
#endif

