#include "paging.h"
#include <stddef.h>
#include <stdint.h>

static uint64_t pml4[NUM_PML4_ENTRIES] __attribute__((aligned(4096)));
static uint64_t pdpt[NUM_PDPT_ENTRIES] __attribute__((aligned(4096)));

static inline void clear_qwords(void *ptr, size_t size)
{
    uint64_t *p = (uint64_t *)ptr;
    for (size_t i = 0; i < size / sizeof(uint64_t); i++) {
        p[i] = 0;
    }
}

void identity_paging_128GB(void)
{
    clear_qwords(pml4, sizeof(pml4));
    clear_qwords(pdpt, sizeof(pdpt));

    /* PML4[0] -> PDPT */
    pml4[0] = (uint64_t)pdpt
            | PAGE_PRESENT
            | PAGE_RW
            | PAGE_USER;

    for (int i = 0; i < 128; i++) {
        uint64_t addr = (uint64_t)i << 30; /* 1 GiB pages */
        pdpt[i] = addr
                | PAGE_PRESENT
                | PAGE_RW
                | PAGE_USER
                | PAGE_PS;
    }

    /* Enable new page table root */
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(pml4)
        : "memory"
    );
}
