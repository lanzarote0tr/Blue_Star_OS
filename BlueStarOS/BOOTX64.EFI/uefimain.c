#include "uefiutil.h"
#include <stdint.h>
#include "../bootinfo.h"

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    uefi_init(ImageHandle, SystemTable);

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

    load_os();

    boot_info_t *bootinfo = BOOT_INFO_ADDR;

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = init_gui();
    bootinfo->framebuffer_base = gop->Mode->FrameBufferBase;
    bootinfo->framebuffer_height = gop->Mode->Info->VerticalResolution;
    bootinfo->framebuffer_width = gop->Mode->Info->HorizontalResolution;

    EFI_STATUS Status;

    Status = exit_boot_services_and_prepare(ImageHandle);
    if (EFI_ERROR(Status))
    {
        Print(L"Failed to ExitBootServices: %r\n", Status);
        return Status;
    }

    __asm__ volatile (
        "mov $0x170000, %rax\n"
        "mov %rax, %rsp\n"
        "mov %rax, %rbp\n"
    );

    ((void(*)())KERNEL_LOAD_ADDR)(bootinfo);

    for(;;) asm("hlt");
}