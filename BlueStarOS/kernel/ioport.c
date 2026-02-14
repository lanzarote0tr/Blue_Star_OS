#include "ioport.h"

void out8(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" ::"a"(val), "Nd"(port) : "memory");
}

uint8_t in8(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

uint16_t in16(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

void out16(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

void io_wait(void)
{
    __asm__ volatile("outb %%al, $0x80" ::"a"(0) : "memory");
}