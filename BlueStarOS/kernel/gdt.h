#ifndef GDT_H
#define GDT_H

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS 0x1B
#define USER_DS 0x23
#define TSS_SELECTOR 0x28

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

void install_gdt(void);
void tss_set_rsp0(uint64_t rsp0);

#endif
