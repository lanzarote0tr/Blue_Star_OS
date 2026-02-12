#include "gdt.h"

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss64_t;

static uint64_t gdt_table[7] = {
    0x0000000000000000ULL, // 0x00: null

    // Ring 0
    0x00AF9A000000FFFFULL, // 0x08: kernel code (CS)
    0x00CF92000000FFFFULL, // 0x10: kernel data (DS/ES/SS)

    // Ring 3
    0x00AFFA000000FFFFULL, // 0x18: user code
    0x00CFF2000000FFFFULL, // 0x20: user data

    // 64-bit TSS descriptor (low/high qword filled at runtime)
    0x0000000000000000ULL, // 0x28
    0x0000000000000000ULL  // 0x30
};

static tss64_t kernel_tss;
static uint8_t bootstrap_kernel_stack[8192] __attribute__((aligned(16)));

static void clear_tss(void)
{
    uint8_t *tss_bytes = (uint8_t *)&kernel_tss;
    for (uint64_t i = 0; i < sizeof(kernel_tss); i++) {
        tss_bytes[i] = 0;
    }
}

static void install_tss_descriptor(void)
{
    uint64_t base = (uint64_t)&kernel_tss;
    uint32_t limit = (uint32_t)sizeof(kernel_tss) - 1;

    gdt_table[5] =
        ((uint64_t)(limit & 0xFFFFU)) |
        ((base & 0xFFFFFFULL) << 16) |
        (0x89ULL << 40) |
        ((((uint64_t)limit >> 16) & 0xFULL) << 48) |
        (((base >> 24) & 0xFFULL) << 56);
    gdt_table[6] = (base >> 32) & 0xFFFFFFFFULL;
}

void tss_set_rsp0(uint64_t rsp0)
{
    kernel_tss.rsp0 = rsp0;
}

void install_gdt(void)
{
    clear_tss();
    kernel_tss.iomap_base = sizeof(kernel_tss);
    tss_set_rsp0((uint64_t)(bootstrap_kernel_stack + sizeof(bootstrap_kernel_stack)));
    install_tss_descriptor();

    gdtr_t gdtr = {
        .limit = sizeof(gdt_table) - 1,
        .base = (uint64_t)&gdt_table
    };

    /* load GDTR */
    __asm__ volatile("lgdt %0" ::"m"(gdtr) : "memory");

    /* Far return (lretq) to reload CS, then reload data segment registers.
       This sequence is compatible with 64-bit long mode usage. */
    __asm__ volatile(
        "pushq $0x08\n\t" /* selector for code (entry 1) */
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t" /* reload CS with 0x08 and jump to label 1 */
        "1:\n\t"
        "mov $0x10, %%ax\n\t" /* selector for data (entry 2) */
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t" ::: "rax", "memory");

    {
        uint16_t tss_sel = TSS_SELECTOR;
        __asm__ volatile("ltr %w0" : : "r"(tss_sel) : "memory");
    }
}
