#ifndef _MFTP_SHARED_ALLOCATOR_H_
#define _MFTP_SHARED_ALLOCATOR_H_

#include <stddef.h>

typedef void* (*alloc_fn)(size_t);
typedef void (*free_fn)(void*);
typedef void* (*realloc_fn)(void*, size_t);

typedef struct {
    alloc_fn alloc;
    free_fn free;
    realloc_fn realloc;
} allocator_t;

extern const allocator_t std_allocator; // instanciated in utils.c

#endif
