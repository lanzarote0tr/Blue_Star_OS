#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

int heap_init(void *base, size_t size);
void* malloc(size_t size);
void free(void* ptr);

#endif
