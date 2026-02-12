#include "pit.h"
#include "ioport.h"

#define PIT_COMMAND_PORT  0x43
#define PIT_CHANNEL0_PORT 0x40
#define PIT_BASE_FREQ     1193182U

void pit_set_frequency(uint32_t freq_hz)
{
    if (freq_hz == 0) {
        return;
    }

    uint32_t divisor32 = PIT_BASE_FREQ / freq_hz;
    if (divisor32 == 0) {
        divisor32 = 1;
    } else if (divisor32 > 0xFFFFu) {
        divisor32 = 0xFFFFu;
    }

    uint16_t divisor = (uint16_t)divisor32;
    out8(PIT_COMMAND_PORT, 0x36); // ch0, lobyte/hibyte, mode3
    io_wait();
    out8(PIT_CHANNEL0_PORT, (uint8_t)(divisor & 0xFF));
    io_wait();
    out8(PIT_CHANNEL0_PORT, (uint8_t)((divisor >> 8) & 0xFF));
}
