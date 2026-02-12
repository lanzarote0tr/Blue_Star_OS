#include "uefiutil.h"
#include <stdint.h>
#include <Library/BaseMemoryLib.h>
#include "../bootinfo.h"

#define BOOT_STACK_ADDR      0x170000ULL
#define BOOT_STACK_SIZE      (64ULL * 1024ULL)
#define IDENTITY_MAP_MAX_PHYS ((16ULL * 1024ULL * 1024ULL * 1024ULL) - 1ULL)
#define KERNEL_HEAP_TARGET_SIZE (256ULL * 1024ULL * 1024ULL)
#define KERNEL_HEAP_MIN_SIZE    (8ULL * 1024ULL * 1024ULL)

static EFI_STATUS allocate_pages_fixed_or_below_16g(
    EFI_PHYSICAL_ADDRESS preferred,
    UINTN pages,
    EFI_PHYSICAL_ADDRESS *addr_out)
{
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS addr = preferred;

    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &addr);
    if (!EFI_ERROR(Status)) {
        *addr_out = addr;
        return EFI_SUCCESS;
    }

    addr = (EFI_PHYSICAL_ADDRESS)IDENTITY_MAP_MAX_PHYS;
    Status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &addr);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    *addr_out = addr;
    return EFI_SUCCESS;
}

static EFI_STATUS fill_boot_info(boot_info_t *bootinfo, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    UINTN bytes_per_pixel;
    UINT64 total_pixels;

    if (bootinfo == NULL || gop == NULL || gop->Mode == NULL || gop->Mode->Info == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    info = gop->Mode->Info;
    total_pixels = (UINT64)info->PixelsPerScanLine * (UINT64)info->VerticalResolution;
    if (total_pixels == 0) {
        return EFI_DEVICE_ERROR;
    }

    bytes_per_pixel = (UINTN)(gop->Mode->FrameBufferSize / total_pixels);
    if (bytes_per_pixel != sizeof(UINT32)) {
        return EFI_UNSUPPORTED;
    }

    bootinfo->framebuffer_base = gop->Mode->FrameBufferBase;
    bootinfo->framebuffer_width = info->HorizontalResolution;
    bootinfo->framebuffer_height = info->VerticalResolution;
    bootinfo->framebuffer_pitch = info->PixelsPerScanLine * sizeof(UINT32);
    bootinfo->framebuffer_bpp = (uint32_t)(bytes_per_pixel * 8U);

    switch (info->PixelFormat) {
    case PixelBlueGreenRedReserved8BitPerColor:
        bootinfo->framebuffer_format = FB_FORMAT_BGRX;
        break;
    case PixelRedGreenBlueReserved8BitPerColor:
        bootinfo->framebuffer_format = FB_FORMAT_RGBX;
        break;
    case PixelBitMask:
        if (info->PixelInformation.RedMask == 0x00FF0000U &&
            info->PixelInformation.GreenMask == 0x0000FF00U &&
            info->PixelInformation.BlueMask == 0x000000FFU) {
            bootinfo->framebuffer_format = FB_FORMAT_BGRX;
            break;
        }
        if (info->PixelInformation.RedMask == 0x000000FFU &&
            info->PixelInformation.GreenMask == 0x0000FF00U &&
            info->PixelInformation.BlueMask == 0x00FF0000U) {
            bootinfo->framebuffer_format = FB_FORMAT_RGBX;
            break;
        }
        bootinfo->framebuffer_format = FB_FORMAT_UNKNOWN;
        return EFI_UNSUPPORTED;
    default:
        bootinfo->framebuffer_format = FB_FORMAT_UNKNOWN;
        return EFI_UNSUPPORTED;
    }

    return EFI_SUCCESS;
}

static EFI_STATUS allocate_kernel_heap_below_16g(
    EFI_PHYSICAL_ADDRESS *heap_addr_out,
    UINTN *heap_pages_out)
{
    EFI_STATUS Status = EFI_NOT_FOUND;
    UINT64 heap_size = KERNEL_HEAP_TARGET_SIZE;

    if (heap_addr_out == NULL || heap_pages_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    while (heap_size >= KERNEL_HEAP_MIN_SIZE) {
        EFI_PHYSICAL_ADDRESS addr = (EFI_PHYSICAL_ADDRESS)IDENTITY_MAP_MAX_PHYS;
        UINTN pages = EFI_SIZE_TO_PAGES(heap_size);

        Status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &addr);
        if (!EFI_ERROR(Status)) {
            *heap_addr_out = addr;
            *heap_pages_out = pages;
            return EFI_SUCCESS;
        }

        heap_size >>= 1;
    }

    return Status;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    boot_info_t *bootinfo;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_PHYSICAL_ADDRESS kernel_entry = (EFI_PHYSICAL_ADDRESS)KERNEL_LOAD_ADDR;
    EFI_PHYSICAL_ADDRESS bootinfo_phys = 0;
    EFI_PHYSICAL_ADDRESS stack_phys = 0;
    EFI_PHYSICAL_ADDRESS heap_phys = 0;
    EFI_PHYSICAL_ADDRESS stack_top = 0;
    UINTN bootinfo_pages = EFI_SIZE_TO_PAGES(sizeof(boot_info_t));
    UINTN stack_pages = EFI_SIZE_TO_PAGES(BOOT_STACK_SIZE);
    UINTN heap_pages = 0;
    BOOLEAN bootinfo_allocated = FALSE;
    BOOLEAN stack_allocated = FALSE;
    BOOLEAN heap_allocated = FALSE;

    uefi_init(ImageHandle, SystemTable);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

    Status = allocate_pages_fixed_or_below_16g((EFI_PHYSICAL_ADDRESS)BOOT_INFO_ADDR, bootinfo_pages, &bootinfo_phys);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate boot info page: %r\n", Status);
        return Status;
    }
    bootinfo_allocated = TRUE;

    Status = allocate_pages_fixed_or_below_16g((EFI_PHYSICAL_ADDRESS)BOOT_STACK_ADDR, stack_pages, &stack_phys);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate boot stack pages: %r\n", Status);
        goto cleanup;
    }
    stack_allocated = TRUE;
    SetMem((VOID *)(UINTN)stack_phys, EFI_PAGES_TO_SIZE(stack_pages), 0);

    Status = allocate_kernel_heap_below_16g(&heap_phys, &heap_pages);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to allocate kernel heap pages: %r\n", Status);
        goto cleanup;
    }
    heap_allocated = TRUE;
    SetMem((VOID *)(UINTN)heap_phys, EFI_PAGES_TO_SIZE(heap_pages), 0);

    stack_top = (EFI_PHYSICAL_ADDRESS)(((UINT64)stack_phys + EFI_PAGES_TO_SIZE(stack_pages)) & ~0xFULL);
    bootinfo = (boot_info_t *)(UINTN)bootinfo_phys;
    SetMem(bootinfo, EFI_PAGES_TO_SIZE(bootinfo_pages), 0);

    Status = load_os(ImageHandle, &kernel_entry);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to load kernel: %r\n", Status);
        goto cleanup;
    }

    gop = init_gui();
    if (gop == NULL) {
        Print(L"Failed to initialize graphics output\n");
        Status = EFI_DEVICE_ERROR;
        goto cleanup;
    }

    Status = fill_boot_info(bootinfo, gop);
    if (EFI_ERROR(Status)) {
        Print(L"Unsupported GOP configuration for kernel framebuffer: %r\n", Status);
        goto cleanup;
    }
    bootinfo->heap_base = (uint64_t)heap_phys;
    bootinfo->heap_size = (uint64_t)EFI_PAGES_TO_SIZE(heap_pages);

    Status = exit_boot_services_and_prepare(ImageHandle);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to ExitBootServices: %r\n", Status);
        goto cleanup;
    }

    __asm__ volatile (
        "mov %0, %%rax\n"
        "mov %%rax, %%rsp\n"
        "mov %%rax, %%rbp\n"
        :
        : "r"(stack_top)
        : "rax", "memory"
    );

    ((void (*)(boot_info_t *))(UINTN)kernel_entry)(bootinfo);

    for (;;)
        __asm__ volatile("hlt");

cleanup:
    if (heap_allocated) {
        gBS->FreePages(heap_phys, heap_pages);
    }
    if (stack_allocated) {
        gBS->FreePages(stack_phys, stack_pages);
    }
    if (bootinfo_allocated) {
        gBS->FreePages(bootinfo_phys, bootinfo_pages);
    }
    return Status;
}
