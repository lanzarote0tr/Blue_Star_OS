#include <stdint.h>
#include <stdbool.h>
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "ioport.h"
#include "multitask.h"
#include "pit.h"
#include "gui.h"
#include "heap.h"
#include "paging.h"
#include "../bootinfo.h"

#define IRQ0_VECTOR 0x20 /* after PIC remap */
#define NULL (void*)0

#define KB  (1024ULL)
#define MB  (1024ULL * KB)
#define GB  (1024ULL * MB)

void task_func_a(void)
{
    while (1)
    {
        for (int k = 0; k < 256; k++) {
            draw_rectangle(0, 0, 200, 200, k, 0, 0);
            gui_update();
        }
        for (int k = 255; k >= 0; k--) {
            draw_rectangle(0, 0, 200, 200, k, 0, 0);
            gui_update();
        }
    }
}

void task_func_b(void)
{
    while (1)
    {
        for (int k = 0; k < 256; k++) {
            draw_rectangle(200, 200, 200, 200, 0, k, 0);
            gui_update();
        }
        for (int k = 255; k >= 0; k--){
            draw_rectangle(200, 200, 200, 200, 0, k, 0);
            gui_update();
        }
    }
}

__attribute__((section(".entry")))
void main(boot_info_t* bootinfo){

    init_gui(bootinfo);

    identity_paging_16GB();

    install_gdt();
    load_idt();

    pic_remap(0x20, 0x28);
    pic_irq_unmask(0);
    
    task_add(task_func_a, 0, KERNEL_CS, KERNEL_DS);
    task_add(task_func_b, 1, KERNEL_CS, KERNEL_DS);

    set_idt_entry(IRQ0_VECTOR, irq0_task_switch, 0, 0x8E, KERNEL_CS);
    pit_set_frequency(50);

    // Switch to first task by setting RSP to its saved area and iretq

    multitask_start();
}