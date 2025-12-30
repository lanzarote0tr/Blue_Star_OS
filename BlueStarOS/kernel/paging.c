#include "paging.h"
#include <stdint.h>
#include <stddef.h>

__attribute__((aligned(4096))) uint64_t pml4[512] = {0, };
__attribute__((aligned(4096))) uint64_t pdpt[512] = {0, };

static inline void zero_memset(void *ptr, size_t size) {
    uint64_t *p = (uint64_t *)ptr;
    for (size_t i = 0; i < size / sizeof(uint64_t); i++) {
        p[i] = 0;
    }
}

void identity_paging_16GB(void)
{
    zero_memset(pml4, sizeof(pml4));
    zero_memset(pdpt, sizeof(pdpt));

    // PML4[0] → PDPT
    pml4[0] = (uint64_t)pdpt
            | PAGE_PRESENT
            | PAGE_RW;

    for (int i = 0; i < 16; i++) {
        // 1GB 단위 물리/선형 주소
        uint64_t addr = (uint64_t)i << 30; // 1GB = 1 << 30

        // PDPTE: 1GB huge page
        // - addr은 1GB 정렬이어야 함 (하위 30비트 0).[web:67]
        // - PS 비트(보통 bit 7)를 PAGE_PS로 가정.
        pdpt[i] = addr
                | PAGE_PRESENT
                | PAGE_RW
                | PAGE_PS;  // 1GB page
    }

    // 새 페이지 테이블 활성화
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(pml4)
        : "memory"
    );
}
