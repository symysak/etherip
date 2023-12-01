#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

extern void * memory_alloc(size_t size);
extern void memory_free(void * ptr);

#endif