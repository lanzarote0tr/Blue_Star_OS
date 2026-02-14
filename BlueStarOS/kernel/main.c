#include <stdint.h>
#include <stdbool.h>
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "multitask.h"
#include "pit.h"
#include "gui.h"
#include "heap.h"
#include "paging.h"
#include "ioport.h"
#include "disk.h"
#include "../bootinfo.h"

#include "FAT.h"

#define CR0_MP_BIT (1ULL << 1)
#define CR0_EM_BIT (1ULL << 2)
#define CR0_TS_BIT (1ULL << 3)
#define CR4_OSFXSR_BIT (1ULL << 9)
#define CR4_OSXMMEXCPT_BIT (1ULL << 10)

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

void fat_example(void) {
    fat_fs_t fs;
    fat_file_t f;
    fat_mount_config_t cfg = {
        .device = {
            .user = 0,
            .read_blocks = disk_read,
            .write_blocks = disk_write
        },
        .partition_lba = 2048
    };

    if (fat_mount(&fs, &cfg) != FAT_OK) return;

    /*

    int64_t status = fat_mount(&fs, &cfg);
    __asm__ volatile(
        "mov %0, %%rdx\n\t"
        : : "r"(status)
    );

    asm volatile("ud2");

    */

    /* 생성+쓰기 (LFN 자동 처리) */


    int64_t status = fat_open(&fs, &f, "/logs/부팅 로그 2026-02-13.txt",
                         FAT_O_WRITE | FAT_O_CREATE | FAT_O_APPEND);

    if (status == FAT_OK) {
        const char *msg = "hello\n";
        size_t written = 0;
        fat_write(&f, msg, 6, &written);
        fat_close(&f);
    }

    /* 읽기 */
    if (fat_open(&fs, &f, "/logs/부팅 로그 2026-02-13.txt", FAT_O_READ) == FAT_OK) {
        char buf[128];
        size_t n = 0;
        fat_read(&f, buf, sizeof(buf), &n);
        fat_close(&f);
    }

    fat_unmount(&fs);
}

__attribute__((section(".entry")))
void main(boot_info_t *bootinfo)
{
    __asm__ volatile("cli");
    init_fpu_sse();

    if (bootinfo == 0) {
        while(true);
    }

    if (!heap_init((void *)(uintptr_t)bootinfo->heap_base, (size_t)bootinfo->heap_size)) {
        while(true);
    }

    init_gui(bootinfo);

    identity_paging_128GB();

    install_gdt();
    //load_idt();

    //set_idt_entry(6, 0, 0, 0, 0);
    //set_idt_entry(8, 0, 0, 0, 0);

    //fat_example();

    pic_remap(0x20, 0x28);

    while(true);

    set_idt_entry(20, irq0_task_switch, 0, 0x8E, KERNEL_CS);
    pit_set_frequency(50);
    pic_irq_unmask(0);

    //task_add(task_func_a, 0, USER_CS, USER_DS);
    //task_add(task_func_b, 1, USER_CS, USER_DS);

    multitask_start();
}
