#include "pic.h"
#include "ioport.h"

#define PIC_IRQ_LINES 16
#define PIC_MASTER_IRQS 8
#define PIC_CASCADE_IRQ 2

void pic_remap(int offset1, int offset2)
{
    if ((offset1 & 0x7) != 0 || (offset2 & 0x7) != 0) {
        return;
    }

    uint8_t a1 = in8(PIC1_DATA);
    uint8_t a2 = in8(PIC2_DATA);

    out8(PIC1_CMD, 0x11);
    io_wait();
    out8(PIC2_CMD, 0x11);
    io_wait();

    out8(PIC1_DATA, (uint8_t)offset1);
    io_wait();
    out8(PIC2_DATA, (uint8_t)offset2);
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
    if (irq >= PIC_MASTER_IRQS) {
        out8(PIC2_CMD, PIC_EOI);
    }
    out8(PIC1_CMD, PIC_EOI);
}

void pic_irq_unmask(int irq_number)
{
    if (irq_number < 0 || irq_number >= PIC_IRQ_LINES) {
        return;
    }

    uint16_t port = (irq_number < PIC_MASTER_IRQS) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)((irq_number < PIC_MASTER_IRQS) ? irq_number : (irq_number - PIC_MASTER_IRQS));
    uint8_t mask = in8(port);
    mask &= (uint8_t)~(1u << bit);
    out8(port, mask);

    if (irq_number >= PIC_MASTER_IRQS) {
        uint8_t master_mask = in8(PIC1_DATA);
        master_mask &= (uint8_t)~(1u << PIC_CASCADE_IRQ);
        out8(PIC1_DATA, master_mask);
    }
}
