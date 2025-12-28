// paging.c - x86_64 paging implementation

#include <stdint.h>
#include <string.h>
#include "paging.h"

static size_t pt_pool_idx = 0;

void *__memset_chk(void *dest, int c, size_t len, size_t destlen) {
    if (len > destlen) {
        for (;;)
            __asm__ volatile ("hlt");
    }

    uint8_t *p = (uint8_t *)dest;
    uint8_t v8 = (uint8_t)c;

    // 정렬될 때까지
    while (((uintptr_t)p & 7) && len) {
        *p++ = v8;
        len--;
    }

    uint64_t v64 = 0;
    for (int i = 0; i < 8; i++)
        v64 = (v64 << 8) | v8;

    uint64_t *p64 = (uint64_t *)p;
    while (len >= 8) {
        *p64++ = v64;
        len -= 8;
    }

    p = (uint8_t *)p64;
    while (len--) {
        *p++ = v8;
    }

    return dest;
}

static inline uint64_t build_flags(
    bool writable,
    bool user,
    bool global,
    bool write_through,
    bool cache_disable,
    bool nx
) {
    uint64_t flags = PF_PRESENT;

    if (writable)       flags |= PF_WRITABLE;
    if (user)           flags |= PF_USER;
    if (global)         flags |= PF_GLOBAL;
    if (write_through)  flags |= PF_PWT;
    if (cache_disable)  flags |= PF_PCD;
    if (nx)             flags |= PF_NX;

    return flags;
}

void load_pml4(uint8_t* pt_pool) {
    __asm__ volatile("mov %0, %%cr3" :: "r"((uint64_t)pt_pool));
}

// Allocate one page for a page table, return its address (identity-mapped)
static page_table_t *alloc_page_table(uint8_t* pt_pool) {
    if (pt_pool_idx + PAGE_SIZE_4K > sizeof(pt_pool)) {
        // Out of memory – real kernel should handle this
        return NULL;
    }
    page_table_t *table = (page_table_t *)&pt_pool[pt_pool_idx];
    pt_pool_idx += PAGE_SIZE_4K;
    memset(table, 0, PAGE_SIZE_4K);
    return table;
}

// Top-level PML4 page
static page_table_t *pml4 = NULL;

// Initialize paging: create PML4 and do identity mapping
void paging_init(uint8_t* pt_pool) {
    // Allocate a fresh PML4 page
    pml4 = alloc_page_table(pt_pool);
    // Identity-map first 128MB using 4KB pages (example range)
    const uint64_t IDENTITY_MAP_SIZE = 128 * 1024 * 1024;
    for (uint64_t addr = 0; addr < IDENTITY_MAP_SIZE; addr += PAGE_SIZE_4K) {
        paging_map_4k(pt_pool, addr, addr, true, false, false, true, false, false);
    }
    // Load the new page table base
    load_pml4((uint64_t)pml4);
}

// Helper: get next-level table from an entry, allocating if needed
static page_table_t *get_or_alloc_table(uint8_t* pt_pool, uint64_t *entry) {
    if (!(*entry & PF_PRESENT)) {
        // Not present: allocate a new table
        page_table_t *new_table = alloc_page_table(pt_pool);
        if (!new_table) return NULL;
        *entry = ((uint64_t)new_table & 0x000ffffffffff000ULL) | PF_PRESENT | PF_WRITABLE;
        return new_table;
    }
    // Return existing table address (mask off flags)
    return (page_table_t *)(*entry & 0x000ffffffffff000ULL);
}

void paging_map_4k(
    uint8_t* pt_pool,
    uint64_t vaddr,
    uint64_t paddr,
    bool writable,
    bool user,
    bool global,
    bool write_through,
    bool cache_disable,
    bool nx
) {
    size_t i4 = (vaddr >> 39) & 0x1FF;
    size_t i3 = (vaddr >> 30) & 0x1FF;
    size_t i2 = (vaddr >> 21) & 0x1FF;
    size_t i1 = (vaddr >> 12) & 0x1FF;

    page_table_t *pdpt = get_or_alloc_table(pt_pool, &pml4->entries[i4]);
    if (!pdpt) return;

    page_table_t *pd = get_or_alloc_table(pt_pool, &pdpt->entries[i3]);
    if (!pd) return;

    page_table_t *pt = get_or_alloc_table(pt_pool, &pd->entries[i2]);
    if (!pt) return;

    uint64_t entry =
        (paddr & 0x000ffffffffff000ULL) |
        build_flags(writable, user, global,
                    write_through, cache_disable, nx);

    pt->entries[i1] = entry;
}

void paging_map_2m(
    uint8_t* pt_pool,
    uint64_t vaddr,
    uint64_t paddr,
    bool writable,
    bool user,
    bool global,
    bool write_through,
    bool cache_disable,
    bool nx
) {
    size_t i4 = (vaddr >> 39) & 0x1FF;
    size_t i3 = (vaddr >> 30) & 0x1FF;
    size_t i2 = (vaddr >> 21) & 0x1FF;

    page_table_t *pdpt = get_or_alloc_table(pt_pool, &pml4->entries[i4]);
    if (!pdpt) return;

    page_table_t *pd = get_or_alloc_table(pt_pool, &pdpt->entries[i3]);
    if (!pd) return;

    uint64_t entry =
        (paddr & 0x000ffffffe00000ULL) |
        PF_SIZE |
        build_flags(writable, user, global,
                    write_through, cache_disable, nx);

    pd->entries[i2] = entry;
}


void paging_map_1g(
    uint8_t* pt_pool,
    uint64_t vaddr,
    uint64_t paddr,
    bool writable,
    bool user,
    bool global,
    bool write_through,
    bool cache_disable,
    bool nx
) {
    size_t i4 = (vaddr >> 39) & 0x1FF;
    size_t i3 = (vaddr >> 30) & 0x1FF;

    page_table_t *pdpt = get_or_alloc_table(pt_pool, &pml4->entries[i4]);
    if (!pdpt) return;

    uint64_t entry =
        (paddr & 0x000ffffc00000000ULL) |
        PF_SIZE |
        build_flags(writable, user, global,
                    write_through, cache_disable, nx);

    pdpt->entries[i3] = entry;
}
