#include "idt.h"
#define IDT_ENTRIES 256
#define KERNEL_CS 0x08

idt_entry_t idt[IDT_ENTRIES];

void set_idt_simple_entry(int vec, void (*handler)()){
    set_idt_entry(vec, handler, 0, 0x8E, 0x08);
}

void set_idt_entry(int vec, void (*handler)(), uint8_t ist, uint8_t type_attr, uint16_t sel)
{
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low = addr & 0xFFFF;
    idt[vec].selector = sel;
    idt[vec].ist = ist & 0x7;
    idt[vec].type_attr = type_attr;
    idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].zero = 0;
}

void load_idt(void)
{
    struct idtr_s idtr = {.limit = sizeof(idt) - 1, .base = (uint64_t)&idt};
    __asm__ volatile("lidt %0" ::"m"(idtr) : "memory");
    for (int i = 0; i < IDT_ENTRIES; i++) set_idt_simple_entry(i, default_handler);
}

__attribute__((naked)) void default_handler(void)
{
    __asm__ volatile(
        "pushq %%rax\n\t"
        "pushq %%rcx\n\t"
        "pushq %%rdx\n\t"
        "pushq %%rbx\n\t"
        "pushq %%rbp\n\t"
        "pushq %%rsi\n\t"
        "pushq %%rdi\n\t"
        "pushq %%r8\n\t"
        "pushq %%r9\n\t"
        "pushq %%r10\n\t"
        "pushq %%r11\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"

        "movb $0x20, %%al\n\t"
        "outb %%al, $0x20\n\t"

        /* pop registers from new stack (scheduler set rsp) */
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%r11\n\t"
        "popq %%r10\n\t"
        "popq %%r9\n\t"
        "popq %%r8\n\t"
        "popq %%rdi\n\t"
        "popq %%rsi\n\t"
        "popq %%rbp\n\t"
        "popq %%rbx\n\t"
        "popq %%rdx\n\t"
        "popq %%rcx\n\t"
        "popq %%rax\n\t"

        "iretq\n\t" ::: "memory");
}
