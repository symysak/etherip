#include <stddef.h>
#include <stdlib.h>

#include "memory.h"

extern void * memory_alloc(size_t size){
    return malloc(size);
}

extern void memory_free(void *ptr){
    free(ptr);
}