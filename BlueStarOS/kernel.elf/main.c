#include <stdint.h>
#include <stdbool.h>
#include <Library/UefiBootServicesTableLib.h>
#include "uefiutil.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "ioport.h"
#include "multitask.h"
#include "pit.h"
#include "gui.h"
#include "heap.h"
#include "paging.h"
#include "bootinfo.h"

#define IRQ0_VECTOR 0x20 /* after PIC remap */

#define KB  (1024ULL)
#define MB  (1024ULL * KB)
#define GB  (1024ULL * MB)

#define PTP_POOL_PAGES 32

uint8_t pt_pool[PAGE_SIZE_4K * PTP_POOL_PAGES] __attribute__((aligned(4096)));

void kernel_main();

void task_func_a(void)
{
    while (1)
    {
        for (int k = 0; k < 256; k++) {
            draw_rectangle(0, 0, gui_width(), gui_height(), k, 0, 0);
            gui_update();
        }
        for (int k = 255; k >= 0; k--) {
            draw_rectangle(0, 0, gui_width(), gui_height(), k, 0, 0);
            gui_update();
        }
    }
}

void task_func_b(void)
{
    while (1)
    {
        for (int k = 0; k < 256; k++) {
            draw_rectangle(500, 500, 500, 500, 0, k, 0);
            gui_update();
        }
        for (int k = 255; k >= 0; k--){
            draw_rectangle(500, 500, 500, 500, 0, k, 0);
            gui_update();
        }
    }
}

void identity_paging_map(){
    paging_init(pt_pool);

    for (int i = 0; i < 4 * GB; i += GB){
        paging_map_1g(pt_pool, i, i, true, false, false, true, false, false);
    }

    if (pt_pool == NULL) for(;;) asm("hlt");

    load_pml4((uint8_t*)pt_pool);
}

void main(boot_info_t* bootinfo){
    init_gui(bootinfo);

    identity_paging_map();

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