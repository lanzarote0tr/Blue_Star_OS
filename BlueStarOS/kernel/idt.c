#include "idt.h"
#include "asmtools.h"
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
    PUSHAQ();
    SEND_EOI();
    POPAQ();

    IRETQ();
}
