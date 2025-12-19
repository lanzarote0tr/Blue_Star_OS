#include "gdt.h"

uint64_t gdt_table[3] = {
    0x0000000000000000ULL, // null
    0x00AF9A000000FFFFULL, // 64-bit code:  0x00 AF 9A 00 00 00 FF FF
    0x00CF92000000FFFFULL  // 64-bit data:  0x00 AF 92 00 00 00 FF FF
};

void install_gdt(void)
{
    struct gdtr gdtr;
    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base = (uint64_t)&gdt_table;

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
}