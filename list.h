#ifndef _LIST_H
#define _LIST_H

typedef struct list_head 
{
	struct list_head *next;
	struct list_head *prev;
}list_h, list_n, *p_list_t;


static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add(struct list_head *pnew,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = pnew;
	pnew->next = next;
	pnew->prev = prev;
	prev->next = pnew;
}


static inline void list_add(struct list_head *pnew, struct list_head *head)
{
	__list_add(pnew, head, head->next);
}


static inline void list_add_tail(struct list_head *pnew, struct list_head *head)
{
	__list_add(pnew, head->prev, head);
}


/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}


static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = (struct list_head *)0;
	entry->prev = (struct list_head *)0;
}


/**
 * list_is_last - tests whether @list is the last entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int list_is_last(const struct list_head *list,
				const struct list_head *head)
{
	return list->next == head;
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}


/**
 * list_is_singular - tests whether a list has just one entry.
 * @head: the list to test.
 */
static inline int list_is_singular(const struct list_head *head)
{
	return !list_empty(head) && (head->next == head->prev);
}


/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop cursor.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)


/*
  * calculate the container entry pointer from pointer to dlist or hlist.
  * hlist MUST BE the first entry and dlist MUST BE
  * the second entry in the structure.
*/
#define container_of_dlist(pdlist, type) ((type *)pdlist)
#define container_of_hlist(phlist, type) ( (type *)(phlist-1))

/*
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of_hlist(phlist, type) ( (type *)((char *)phlist-offsetof(type,hlist)))
*/


#endif
