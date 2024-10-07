#ifndef _MFTP_SHARED_LIST_H_
#define _MFTP_SHARED_LIST_H_

/* Simple doubly linked list implementation */
/* List takes ownership of the data - memory for it should be managed using lis_remove and list_clear */

#include <stddef.h>
#include <limits.h>
#include <stdbool.h>

#include "allocator.h"

typedef struct list_node {
    struct list_node *prev;
    struct list_node *next;
    void* data;
} list_node_t;

typedef struct linked_list {
    list_node_t* head;
    list_node_t* tail;
    size_t size;
    size_t sizeof_element;
    allocator_t allocator;
} list_t;

typedef struct linked_list_iterator {
    list_node_t* current;
} list_iter_t;

list_t list_new_ex(size_t sizeof_element, allocator_t allocator);
#define list_new(type) list_new_ex(sizeof(type), std_allocator)

list_iter_t list_iter(const list_t* list);
void* list_next(list_iter_t* iter);

enum {
    LIST_FRONT = INT_MIN,
    // ...
    LIST_BACK = INT_MAX
};

bool list_insert(list_t* list, const void* data, int where);
void* list_remove(list_t* list, int where);
void* list_get(const list_t* list, int where);
void list_clear(list_t* list);

#endif
