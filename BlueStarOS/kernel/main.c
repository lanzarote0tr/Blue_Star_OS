#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "multitask.h"
#include "pit.h"
#include "gui.h"
#include "heap.h"
#include "paging.h"
#include "../bootinfo.h"

#define IRQ0_VECTOR 0x20 /* after PIC remap */
#define COLOR_MIN 0
#define COLOR_MAX 255

#define CR0_MP_BIT (1ULL << 1)
#define CR0_EM_BIT (1ULL << 2)
#define CR0_TS_BIT (1ULL << 3)
#define CR4_OSFXSR_BIT (1ULL << 9)
#define CR4_OSXMMEXCPT_BIT (1ULL << 10)

static void halt_forever(void)
{
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void fill_screen_red(uint8_t value)
{
    draw_rectangle(0, 0, (int)gui_width(), (int)gui_height(), value, 0, 0);
}

static void init_fpu_sse(void)
{
    uint64_t cr0;
    uint64_t cr4;
    const uint32_t default_mxcsr = 0x1F80U;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_MP_BIT;
    cr0 &= ~CR0_EM_BIT;
    cr0 &= ~CR0_TS_BIT;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSFXSR_BIT;
    cr4 |= CR4_OSXMMEXCPT_BIT;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    __asm__ volatile("fninit");
    __asm__ volatile("ldmxcsr %0" : : "m"(default_mxcsr));
}

void task_func_a(void)
{
    while (1) {
        for (int k = COLOR_MIN; k <= COLOR_MAX; k++) {
            fill_screen_red((uint8_t)k);
            gui_update();
        }
        for (int k = COLOR_MAX; k >= COLOR_MIN; k--) {
            fill_screen_red((uint8_t)k);
            gui_update();
        }
    }
}

void task_func_b(void)
{
    while (1) {
        for (int k = COLOR_MIN; k <= COLOR_MAX; k++) {
            draw_rectangle(200, 200, 200, 200, 0, (uint8_t)k, 0);
            gui_update();
        }
        for (int k = COLOR_MAX; k >= COLOR_MIN; k--) {
            draw_rectangle(200, 200, 200, 200, 0, (uint8_t)k, 0);
            gui_update();
        }
    }
}

__attribute__((section(".entry")))
void main(boot_info_t *bootinfo)
{
    __asm__ volatile("cli");
    init_fpu_sse();

    if (bootinfo == 0) {
        halt_forever();
    }

    if (!heap_init((void *)(uintptr_t)bootinfo->heap_base, (size_t)bootinfo->heap_size)) {
        halt_forever();
    }

    init_gui(bootinfo);

    identity_paging_128GB();

    install_gdt();
    load_idt();

    set_idt_entry(IRQ0_VECTOR, irq0_task_switch, 0, 0x8E, KERNEL_CS);
    pic_remap(0x20, 0x28);
    pit_set_frequency(50);
    pic_irq_unmask(0);

    task_add(task_func_a, 0, USER_CS, USER_DS);
    // task_add(task_func_b, 1, USER_CS, USER_DS);

    multitask_start();
}
