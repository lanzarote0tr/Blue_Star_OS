#ifndef GDT_H
#define GDT_H

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

#include <stdint.h>

struct __attribute__((packed)) gdtr
{
    uint16_t limit;
    uint64_t base;
};

void install_gdt(void);

#endif