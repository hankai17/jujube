#include "list.h"
#include "mem.h"
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#include <stdio.h>

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

typedef enum {
	MEM_16B_BUF = 0,
	MEM_32B_BUF,
	MEM_64B_BUF,
	MEM_128B_BUF,
	MEM_256B_BUF,
	MEM_512B_BUF,	
	MEM_1K_BUF,
	MEM_2K_BUF,
	MEM_4K_BUF,
	MEM_8K_BUF,
	MEM_16K_BUF,
	MEM_32K_BUF,
	MEM_64K_BUF,
	MEM_128K_BUF,
	MEM_256K_BUF,
	MEM_512K_BUF,
	MEM_1M_BUF,
	MEM_TYPE_MAX
} mem_type_t;

#ifdef DEBUG_MEM_CHECK
static struct list_head MEM_USED_LIST;  /*this is used for mem check*/
#endif

static struct list_head MemPools[MEM_TYPE_MAX];
static pthread_mutex_t	 mem_mutex;

mem_stat_t mem_stat[MEM_TYPE_MAX];


/* 
or a single linked list

if have statistics
typedef struct  {
	struct list_head pools;
	int highwater;
	int malloctimes;
	.....
} mempool_t;

or

tyepdef struct {
	int highwater;
	int times;
	......
} mempool_stat_t;

static mempool_stat_t MemPoolStat[MEM_TYPE_MAX]
*/

void  mem_module_init()
{
	int i;
	pthread_mutexattr_t mutexattr;

	for( i=0; i<MEM_TYPE_MAX; i++) {
		INIT_LIST_HEAD(&MemPools[i]);
		mem_stat[i].blocks_free = 0;
		mem_stat[i].blocks_used = 0;
	}

	#ifdef DEBUG_MEM_CHECK
	INIT_LIST_HEAD(&MEM_USED_LIST);
	#endif	

	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_settype(&mutexattr,PTHREAD_MUTEX_DEFAULT);	
	pthread_mutex_init(&mem_mutex,&mutexattr);
	pthread_mutexattr_destroy(&mutexattr);	

}


void *  mem_alloc(const char *module, size_t size)
{
	int i;
	mem_entry_t  *me;

	assert(size <= MAX_EXT_MEM_BLOCK );		
	
	for(i = 0; i < MEM_TYPE_MAX; i++){
		if( size <= 1 << (i+4) )
			break;
	}
	assert(i < MEM_TYPE_MAX);

	pthread_mutex_lock(&mem_mutex);	

	if( list_empty(&MemPools[i]) ){
		/*me = (mem_entry_t *)malloc(sizeof(mem_entry_t) + size - 1);*/
		me = (mem_entry_t *)malloc((1 << (i+4)) + offsetof(mem_entry_t, obj) + 1);
		me->obj_size = size;
		me->refcount = 1;
		*(char*)((void*)me+((1 << (i+4)) + offsetof(mem_entry_t, obj))) = 0xf;
		
		#ifdef DEBUG_MEM_CHECK
		if(module)
			me->module = module;
		else
			me->module = 0;
		list_add( &me->dlist, &MEM_USED_LIST );
		/*mem_alloc_time++*/
		/*this value should be equal to the num of entries in usedlist.*/
		#endif

		mem_stat[i].blocks_used++;

		memset((void *)me->obj, 0x0, (1 << (i+4)));
		pthread_mutex_unlock(&mem_mutex);
		return (void *)me->obj;
	}

	me = container_of_dlist(MemPools[i].next, mem_entry_t);
	list_del(MemPools[i].next);
	
	#ifdef DEBUG_MEM_CHECK
	if(module)
		me->module = module;
	else
		me->module = 0;
	list_add( &me->dlist, &MEM_USED_LIST );
	#endif

	mem_stat[i].blocks_used++;
	mem_stat[i].blocks_free--;
	
	me->refcount++;
	assert(me->refcount== 1);
	memset((void *)me->obj, 0x0, (1 << (i+4)));
	pthread_mutex_unlock(&mem_mutex);
	return (void *)me->obj;
}


void  mem_free(void *buf)
{
	int i; 
	mem_entry_t * me = (mem_entry_t *)((char *)buf - offsetof(mem_entry_t, obj));
/*
	debug(20, 0)("mem_free: module(%s), size(%d)\n", me->module, me->obj_size);
	debug_log_flush();
*/
	for(i = 0; i < MEM_TYPE_MAX; i++){
		if( me->obj_size <= 1 << (i+4) )
			break;
	}

	assert( i < MEM_TYPE_MAX );
	assert(*(char*)((void*)me+((1 << (i+4)) + offsetof(mem_entry_t, obj))) == 0xf);

	me->refcount--;
	assert(me->refcount == 0);

	pthread_mutex_lock(&mem_mutex);	
	
	#ifdef DEBUG_MEM_CHECK
	list_del(&me->dlist);
	#endif
	
	list_add_tail(&me->dlist,&MemPools[i]);
	mem_stat[i].blocks_used--;
	mem_stat[i].blocks_free++;
	pthread_mutex_unlock(&mem_mutex);
	//may do some statistics
}

int statistics_mem_info(char buff[], size_t buff_sz)
{
	int i,  fmt_sz, cur_sz = 0;
	int blocks;
	u_int64_t total_mem_sz = 0, cur_mem_block_sz;

	for(i = 0; i < MEM_TYPE_MAX; ++i){
		blocks = mem_stat[i].blocks_used + mem_stat[i].blocks_free;
		if (blocks > 0)
		{
			cur_mem_block_sz = (u_int64_t)blocks*((1<<(i+4) )+ offsetof(mem_entry_t, obj));
			total_mem_sz += cur_mem_block_sz;
			/*block size:total size:used:free*/
			fmt_sz =snprintf(buff+cur_sz, buff_sz -cur_sz , "%d: %lu,%d,%d\n", (1<<(i+4)), cur_mem_block_sz,
						mem_stat[i].blocks_used, mem_stat[i].blocks_free);
			cur_sz += fmt_sz;
		}

		if(cur_sz > buff_sz - 3)
			break;
	}
	
	if(cur_sz < buff_sz)
	{
		fmt_sz = snprintf(buff+cur_sz, buff_sz -cur_sz, "Total size:%lu ", total_mem_sz);
		cur_sz += fmt_sz;
	}

	return cur_sz;	
}

void *  mem_realloc(void *buf, size_t old_size ,size_t new_size)
{
	mem_entry_t * me = (mem_entry_t *)((char *)buf - offsetof(mem_entry_t, obj));
	
	void* new_buf = mem_alloc(me->module, new_size);
	if(old_size<=me->obj_size && old_size<=new_size)
	{
		memcpy(new_buf,buf,old_size);
	}
	mem_free(buf);	
	return new_buf;
}

#ifdef DEBUG_MEM_CHECK
int64_t mem_check_mod_count( const char *module , int *objsize)
{
	if( NULL == module )
		return 0;

	int64_t count = 0;
	struct list_head *cursor;

	mem_entry_t *q = 0;

	pthread_mutex_lock(&mem_mutex);	
	list_for_each(cursor, &MEM_USED_LIST){
		q = container_of_dlist(cursor, mem_entry_t);
		if(q->module != NULL && strcmp(q->module, module) == 0)
		{
			if(0==*objsize) *objsize = q->obj_size;
			count++;
		}
	}
	pthread_mutex_unlock(&mem_mutex);

	return count;
}
#endif

