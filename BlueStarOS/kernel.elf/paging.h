// paging.h - x86_64 paging implementation

#ifndef _PAGING_H_
#define _PAGING_H_

#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE_4K    0x1000ULL
#define PAGE_SIZE_2M    0x200000ULL
#define PAGE_SIZE_1G    0x40000000ULL

// Page table entry flags (bit positions)
#define PF_PRESENT    (1ULL << 0)   // Present
#define PF_WRITABLE   (1ULL << 1)   // Read/Write
#define PF_USER       (1ULL << 2)   // User/Supervisor
#define PF_PWT        (1ULL << 3)   // Page-level Write-Through
#define PF_PCD        (1ULL << 4)   // Page-level Cache Disable
#define PF_ACCESSED   (1ULL << 5)   // Accessed
#define PF_DIRTY      (1ULL << 6)   // Dirty (only in leaf page entries)
#define PF_SIZE       (1ULL << 7)   // Page Size (PS) flag (in PD/PDPT entries)
#define PF_GLOBAL     (1ULL << 8)   // Global page
#define PF_NX         (1ULL << 63)  // No-execute (NX) bit

// A page table contains 512 entries (4KB aligned)
typedef struct {
    uint64_t entries[512];
} __attribute__((aligned(4096))) page_table_t;

// Initialize paging: create a new PML4 and identity-map initial memory.
// (free_mem_ptr is a placeholder for a free identity-mapped region)
void paging_init(uint8_t* pt_pool);

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
);

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
);


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
);

// Load the PML4 base into CR3 (switch page tables)
void load_pml4(uint8_t* pt_pool);

#endif // _PAGING_H_
