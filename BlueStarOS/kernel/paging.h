#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_PS      0x80   // 1GB 페이지

#define NUM_PML4_ENTRIES 512
#define NUM_PDPT_ENTRIES 512

// 16GB identity mapping 초기화
void identity_paging_16GB(void);

#endif
