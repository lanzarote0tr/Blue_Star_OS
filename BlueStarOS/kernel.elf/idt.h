#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef struct
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

struct __attribute__((packed)) idtr_s
{
    uint16_t limit;
    uint64_t base;
};

void set_idt_entry(int vec, void (*handler)(), uint8_t ist, uint8_t type_attr, uint16_t sel);
void set_idt_simple_entry(int vec, void (*handler)());
void load_idt(void);
__attribute__((naked)) void default_handler(void);

#endif