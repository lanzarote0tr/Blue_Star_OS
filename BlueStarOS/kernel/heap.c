#include "heap.h"
#define NULL 0
#define size_t uint64_t

#define MAX_ORDER   24
#define MIN_ORDER   MAX_ORDER - 20
#define HEAP_SIZE   2 << MAX_ORDER

static uint8_t heap_memory[HEAP_SIZE];
static heap_block* free_list[MAX_ORDER + 1];
bool initialized = false;

void heap_init(void)
{
    for (int i = 0; i <= MAX_ORDER; i++)
        free_list[i] = NULL;

    heap_block* initial = (heap_block*)heap_memory;
    initial->used = false;
    initial->order = MAX_ORDER;
    initial->next = NULL;

    free_list[MAX_ORDER] = initial;

    initialized = true;
}

static int size_to_order(size_t size)
{
    size += sizeof(heap_block);
    int order = MIN_ORDER;

    while ((1U << order) < size)
        order++;

    return order;
}


void* malloc(size_t size)
{
    if (!initialized) heap_init();
    
    int order = size_to_order(size);

    for (int i = order; i <= MAX_ORDER; i++) {
        if (free_list[i]) {
            heap_block* block = free_list[i];
            free_list[i] = block->next;

            while (i > order) {
                i--;
                heap_block* buddy =
                    (heap_block*)((uint8_t*)block + (1 << i));

                buddy->used = false;
                buddy->order = i;
                buddy->next = free_list[i];
                free_list[i] = buddy;

                block->order = i;
            }

            block->used = true;
            return (uint8_t*)block + sizeof(heap_block);
        }
    }

    return NULL;
}


void free(void* ptr)
{
    if (!initialized) heap_init();
    
    if (!ptr) return;

    heap_block* block =
        (heap_block*)((uint8_t*)ptr - sizeof(heap_block));

    block->used = false;

    while (block->order < MAX_ORDER) {
        uintptr_t offset =
            (uintptr_t)((uint8_t*)block - heap_memory);
        uintptr_t buddy_offset =
            offset ^ (1 << block->order);

        heap_block* buddy =
            (heap_block*)(heap_memory + buddy_offset);

        if (buddy->used || buddy->order != block->order)
            break;

        // buddy 제거
        heap_block** prev = &free_list[buddy->order];
        while (*prev && *prev != buddy)
            prev = &(*prev)->next;

        if (*prev == NULL)
            break;

        *prev = buddy->next;

        if (buddy < block)
            block = buddy;

        block->order++;
    }

    block->next = free_list[block->order];
    free_list[block->order] = block;
}
