#include <stdint.h>
#include <stdbool.h>
#include "uefiutil.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "ioport.h"
#include "multitask.h"
#include "pit.h"
#include "gui.h"
#include "heap.h"

#define IRQ0_VECTOR 0x20 /* after PIC remap */

uint32_t *fb;
uint64_t width;
uint64_t height;

static void task_func_a(void)
{
    while (1)
    {
        for (int k = 0; k <= 256; k++)
        {
            for (int i = 1; i <= 100; i++)
            {
                for (int j = 1; j <= 100; j++)
                {
                    fb[i * width + j] = k * 0x100;
                }
            }
        }
    }
}

static void task_func_b(void)
{
    while (1)
    {
        for (int k = 0; k <= 256; k++)
        {
            for (int i = 101; i <= 200; i++)
            {
                for (int j = 101; j <= 200; j++)
                {
                    fb[i * width + j] = k;
                }
            }
        }
    }
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{

    EFI_GRAPHICS_OUTPUT_PROTOCOL* GOP = init_gui();
    fb = GOP->Mode->FrameBufferBase;
    width = GOP->Mode->Info->HorizontalResolution;
    height = GOP->Mode->Info->VerticalResolution;

    EFI_STATUS Status;

    Status = exit_boot_services_and_prepare(ImageHandle);
    if (EFI_ERROR(Status))
    {
        Print(L"Failed to ExitBootServices: %r\n", Status);
        return Status;
    }

    install_gdt();
    load_idt();

    pic_remap(0x20, 0x28);
    pic_irq_unmask(0);

    task_add(task_func_a, 0);
    task_add(task_func_b, 1);

    set_idt_entry(IRQ0_VECTOR, irq0_task_switch, 0, 0x8E, KERNEL_CS);
    pit_set_frequency(50);

    // Switch to first task by setting RSP to its saved area and iretq

    multitask_start();

    return EFI_SUCCESS;
}
