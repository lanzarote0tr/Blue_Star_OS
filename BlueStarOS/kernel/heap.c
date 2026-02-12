#include "heap.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_HEAP_ORDER 47
#define MIN_HEAP_SIZE  (1ULL << 12)

typedef struct heap_block {
    struct heap_block *next;
    bool used;
    uint8_t order;
} heap_block;

static uint8_t *heap_memory;
static size_t heap_size;
static int heap_max_order;
static heap_block *free_list[MAX_HEAP_ORDER + 1];
static bool initialized = false;

static inline size_t order_size(int order)
{
    return (size_t)1ULL << (unsigned)order;
}

static int floor_log2_size(size_t value)
{
    int order = -1;

    while (value != 0) {
        value >>= 1;
        order++;
    }

    return order;
}

static inline bool in_heap(const void *p)
{
    uintptr_t off = (uintptr_t)p - (uintptr_t)heap_memory;
    return off < heap_size;
}

static void add_free(heap_block **list, heap_block *b)
{
    b->next = *list;
    *list = b;
}

int heap_init(void *base, size_t size)
{
    if (base == NULL || size < MIN_HEAP_SIZE) {
        initialized = false;
        return 0;
    }

    int order = floor_log2_size(size);
    if (order < 0 || order > MAX_HEAP_ORDER) {
        initialized = false;
        return 0;
    }

    heap_memory = (uint8_t *)base;
    heap_size = order_size(order);
    heap_max_order = order;

    for (int i = 0; i <= MAX_HEAP_ORDER; i++) {
        free_list[i] = NULL;
    }

    heap_block *initial = (heap_block *)heap_memory;
    initial->used = false;
    initial->order = (uint8_t)heap_max_order;
    initial->next = NULL;

    free_list[heap_max_order] = initial;
    initialized = true;
    return 1;
}

static int size_to_order(size_t user_size)
{
    size_t needed;
    int order = 0;

    if (user_size > SIZE_MAX - sizeof(heap_block)) {
        return -1;
    }

    needed = user_size + sizeof(heap_block);

    while (order <= heap_max_order && order_size(order) < needed) {
        order++;
    }

    if (order > heap_max_order) {
        return -1;
    }

    return order;
}

void *malloc(size_t size)
{
    if (!initialized || size == 0) {
        return NULL;
    }

    int order = size_to_order(size);
    if (order < 0) {
        return NULL;
    }

    int i;
    for (i = order; i <= heap_max_order; i++) {
        if (free_list[i] != NULL) {
            break;
        }
    }
    if (i > heap_max_order) {
        return NULL;
    }

    heap_block *block = free_list[i];
    free_list[i] = block->next;
    block->next = NULL;

    while (i > order) {
        i--;

        uintptr_t buddy_addr = (uintptr_t)block + order_size(i);
        if (!in_heap((void *)buddy_addr)) {
            break;
        }

        heap_block *buddy = (heap_block *)buddy_addr;
        buddy->used = false;
        buddy->order = (uint8_t)i;
        buddy->next = NULL;
        add_free(&free_list[i], buddy);

        block->order = (uint8_t)i;
    }

    block->used = true;
    block->order = (uint8_t)order;

    return (void *)((uint8_t *)block + sizeof(heap_block));
}

void free(void *ptr)
{
    if (!initialized || ptr == NULL) {
        return;
    }

    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t heap_base = (uintptr_t)heap_memory;

    if (addr < heap_base + sizeof(heap_block) || addr >= heap_base + heap_size) {
        return;
    }

    heap_block *block = (heap_block *)(addr - sizeof(heap_block));

    if (!in_heap(block) || !block->used) {
        return;
    }

    block->used = false;

    while (block->order < heap_max_order) {
        size_t size_class = order_size(block->order);
        uintptr_t offset = (uintptr_t)block - (uintptr_t)heap_memory;
        uintptr_t buddy_offset = offset ^ size_class;
        if (buddy_offset >= heap_size) {
            break;
        }

        heap_block *buddy = (heap_block *)(heap_memory + buddy_offset);

        if (!in_heap(buddy) || buddy->used || buddy->order != block->order) {
            break;
        }

        heap_block **prev = &free_list[buddy->order];
        while (*prev && *prev != buddy) {
            prev = &(*prev)->next;
        }

        if (*prev != buddy) {
            break;
        }

        *prev = buddy->next;

        if (buddy < block) {
            block = buddy;
        }

        block->order++;
        block->next = NULL;
        block->used = false;
    }

    add_free(&free_list[block->order], block);
}
