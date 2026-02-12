#include "idt.h"
#include "asmtools.h"

#define IDT_ENTRIES 256

static idt_entry_t idt[IDT_ENTRIES];

static int vector_has_error_code(int vec)
{
    switch (vec) {
    case 8:   // #DF
    case 10:  // #TS
    case 11:  // #NP
    case 12:  // #SS
    case 13:  // #GP
    case 14:  // #PF
    case 17:  // #AC
    case 21:  // #CP
    case 29:  // #VC
    case 30:  // #SX
        return 1;
    default:
        return 0;
    }
}

void set_idt_simple_entry(int vec, void (*handler)(void))
{
    set_idt_entry(vec, handler, 0, 0x8E, 0x08);
}

void set_idt_entry(int vec, void (*handler)(void), uint8_t ist, uint8_t type_attr, uint16_t sel)
{
    uint64_t addr = (uint64_t)handler;
    idt[vec].offset_low = addr & 0xFFFF;
    idt[vec].selector = sel;
    idt[vec].ist = ist & 0x7;
    idt[vec].type_attr = type_attr;
    idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFULL);
    idt[vec].zero = 0;
}

void load_idt(void)
{
    for (int i = 0; i < IDT_ENTRIES; i++) {
        if (vector_has_error_code(i)) {
            set_idt_simple_entry(i, default_handler_err);
        } else {
            set_idt_simple_entry(i, default_handler);
        }
    }

    idtr_t idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)&idt
    };
    __asm__ volatile("lidt %0" ::"m"(idtr) : "memory");
}

__attribute__((naked)) void default_handler(void)
{
    PUSHAQ();
    SEND_EOI();
    POPAQ();

    IRETQ();
}

__attribute__((naked)) void default_handler_err(void)
{
    PUSHAQ();
    SEND_EOI();
    POPAQ();
    __asm__ volatile("addq $8, %%rsp" ::: "memory");
    IRETQ();
}
