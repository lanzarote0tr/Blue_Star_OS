#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT 0x001ULL
#define PAGE_RW      0x002ULL
#define PAGE_USER    0x004ULL
#define PAGE_PS      0x080ULL

#define NUM_PML4_ENTRIES 512
#define NUM_PDPT_ENTRIES 512

/* Build identity mapping for the low 128 GiB using 1 GiB pages */
void identity_paging_128GB(void);

#endif
