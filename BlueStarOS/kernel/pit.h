#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/* Program PIT channel 0 to the requested frequency in Hz */
void pit_set_frequency(uint32_t freq_hz);

#endif
