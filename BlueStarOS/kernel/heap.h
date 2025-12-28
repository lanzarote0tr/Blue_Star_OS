#ifndef HEAP_H
#define HEAP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct heap_block {
    struct heap_block* next;
    bool used;
    uint8_t order;
} heap_block;

void* malloc(uint64_t size);
void free(void* ptr);

#endif