#include "pic.h"
#include "ioport.h"

void pic_remap(int offset1, int offset2)
{
    uint8_t a1 = in8(PIC1_DATA);
    uint8_t a2 = in8(PIC2_DATA);

    out8(PIC1_CMD, 0x11);
    io_wait();
    out8(PIC2_CMD, 0x11);
    io_wait();

    out8(PIC1_DATA, offset1);
    io_wait();
    out8(PIC2_DATA, offset2);
    io_wait();

    out8(PIC1_DATA, 0x04);
    io_wait();
    out8(PIC2_DATA, 0x02);
    io_wait();

    out8(PIC1_DATA, 0x01);
    io_wait();
    out8(PIC2_DATA, 0x01);
    io_wait();

    out8(PIC1_DATA, a1);
    out8(PIC2_DATA, a2);
}

void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
        out8(PIC2_CMD, PIC_EOI);
    out8(PIC1_CMD, PIC_EOI);
}

void pic_irq_unmask(int irq_number){
    uint8_t mask = in8(PIC1_DATA);
    mask &= ~(1 << irq_number);
    out8(PIC1_DATA, mask);
}