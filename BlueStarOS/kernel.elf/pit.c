#include "pit.h"
#include "pic.h"
#include "ioport.h"

#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL0_PORT 0x40
#define PIT_BASE_FREQ 1193182

void pit_set_frequency(uint32_t freq_hz)
{
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / freq_hz);
    out8(PIT_COMMAND_PORT, 0x36); // ch0, lobyte/hibyte, mode3
    io_wait();
    out8(PIT_CHANNEL0_PORT, divisor & 0xFF);
    io_wait();
    out8(PIT_CHANNEL0_PORT, (divisor >> 8) & 0xFF);
}