#ifndef IOPORT_H
#define IOPORT_H

#include <stdint.h>

void out8(uint16_t port, uint8_t val);
uint8_t in8(uint16_t port);

uint16_t in16(uint16_t port);
void out16(uint16_t port, uint16_t val);

void io_wait(void);

#endif
