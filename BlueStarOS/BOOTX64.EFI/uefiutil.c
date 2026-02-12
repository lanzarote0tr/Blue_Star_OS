#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#include "../bootinfo.h"

#define EXIT_BOOT_SERVICES_RETRY 2U
#define MEMORY_MAP_EXTRA_DESC 8U
#define PAGE_SIZE_4K 4096ULL
#define KERNEL_PHYS_MAX ((16ULL * 1024ULL * 1024ULL * 1024ULL) - 1ULL)

#define EI_NIDENT 16
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6

#define ELFMAG0 0x7FU
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2U
#define ELFDATA2LSB 1U
#define EV_CURRENT 1U
#define ET_EXEC 2U
#define EM_X86_64 62U
#define PT_LOAD 1U

typedef struct {
    UINT8 e_ident[EI_NIDENT];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} elf64_phdr_t;

static void close_if_open(EFI_FILE_PROTOCOL **fp)
{
    if (*fp != NULL) {
        (*fp)->Close(*fp);
        *fp = NULL;
    }
}

static UINT64 align_down_4k(UINT64 value)
{
    return value & ~(PAGE_SIZE_4K - 1ULL);
}

static EFI_STATUS align_up_4k(UINT64 value, UINT64 *aligned_out)
{
    if (aligned_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (value > (MAX_UINT64 - (PAGE_SIZE_4K - 1ULL))) {
        return EFI_BAD_BUFFER_SIZE;
    }

    *aligned_out = (value + PAGE_SIZE_4K - 1ULL) & ~(PAGE_SIZE_4K - 1ULL);
    return EFI_SUCCESS;
}

static uint32_t detect_framebuffer_format(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info)
{
    if (info == NULL) {
        return FB_FORMAT_UNKNOWN;
    }

    switch (info->PixelFormat) {
    case PixelBlueGreenRedReserved8BitPerColor:
        return FB_FORMAT_BGRX;
    case PixelRedGreenBlueReserved8BitPerColor:
        return FB_FORMAT_RGBX;
    case PixelBitMask:
        if (info->PixelInformation.RedMask == 0x00FF0000U &&
            info->PixelInformation.GreenMask == 0x0000FF00U &&
            info->PixelInformation.BlueMask == 0x000000FFU) {
            return FB_FORMAT_BGRX;
        }
        if (info->PixelInformation.RedMask == 0x000000FFU &&
            info->PixelInformation.GreenMask == 0x0000FF00U &&
            info->PixelInformation.BlueMask == 0x00FF0000U) {
            return FB_FORMAT_RGBX;
        }
        return FB_FORMAT_UNKNOWN;
    default:
        return FB_FORMAT_UNKNOWN;
    }
}

static int framebuffer_format_rank(uint32_t format)
{
    if (format == FB_FORMAT_BGRX) {
        return 2;
    }
    if (format == FB_FORMAT_RGBX) {
        return 1;
    }

    return 0;
}

static EFI_STATUS open_kernel_file(EFI_FILE_PROTOCOL *root, EFI_FILE_PROTOCOL **kernel_out, const CHAR16 **path_out)
{
    static CHAR16 *const candidates[] = {
        L"\\kernel.elf",
        L"\\KERNEL.ELF",
        L"\\EFI\\BOOT\\kernel.elf",
        L"\\EFI\\BOOT\\KERNEL.ELF",
        L"\\kernel",
        L"\\KERNEL",
        L"\\EFI\\BOOT\\KERNEL",
        L"\\EFI\\BOOT\\kernel",
    };

    EFI_STATUS Status = EFI_NOT_FOUND;
    *kernel_out = NULL;
    *path_out = NULL;

    for (UINTN i = 0; i < ARRAY_SIZE(candidates); i++) {
        Status = root->Open(root, kernel_out, candidates[i], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(Status) && *kernel_out != NULL) {
            *path_out = candidates[i];
            return EFI_SUCCESS;
        }
    }

    return EFI_NOT_FOUND;
}

static EFI_STATUS read_file_contents(EFI_FILE_PROTOCOL *file, UINT8 **buffer_out, UINTN *size_out)
{
    EFI_STATUS Status;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = 0;
    UINTN file_size = 0;
    UINT8 *buffer = NULL;
    UINTN bytes_to_read = 0;

    if (file == NULL || buffer_out == NULL || size_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    *buffer_out = NULL;
    *size_out = 0;

    Status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        return Status;
    }

    info = AllocatePool(info_size);
    if (info == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }

    Status = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(Status)) {
        goto cleanup;
    }

    file_size = (UINTN)info->FileSize;
    if (file_size == 0) {
        Status = EFI_INVALID_PARAMETER;
        goto cleanup;
    }

    buffer = AllocatePool(file_size);
    if (buffer == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto cleanup;
    }

    Status = file->SetPosition(file, 0);
    if (EFI_ERROR(Status)) {
        goto cleanup;
    }

    bytes_to_read = file_size;
    Status = file->Read(file, &bytes_to_read, buffer);
    if (EFI_ERROR(Status) || bytes_to_read != file_size) {
        Status = EFI_LOAD_ERROR;
        goto cleanup;
    }

    *buffer_out = buffer;
    *size_out = file_size;
    buffer = NULL;
    Status = EFI_SUCCESS;

cleanup:
    if (buffer != NULL) {
        FreePool(buffer);
    }
    if (info != NULL) {
        FreePool(info);
    }
    return Status;
}

static BOOLEAN is_elf64_kernel(const UINT8 *image, UINTN image_size)
{
    const elf64_ehdr_t *ehdr;

    if (image == NULL || image_size < sizeof(elf64_ehdr_t)) {
        return FALSE;
    }

    ehdr = (const elf64_ehdr_t *)image;
    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        return FALSE;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
        return FALSE;
    }

    if (ehdr->e_machine != EM_X86_64 ||
        ehdr->e_type != ET_EXEC ||
        ehdr->e_version != EV_CURRENT) {
        return FALSE;
    }

    return TRUE;
}

static EFI_STATUS load_elf_kernel(const UINT8 *image,
                                  UINTN image_size,
                                  EFI_PHYSICAL_ADDRESS *entry_out,
                                  EFI_PHYSICAL_ADDRESS *alloc_base_out,
                                  UINTN *alloc_pages_out)
{
    const elf64_ehdr_t *ehdr;
    const elf64_phdr_t *phdrs;
    UINT64 min_addr = MAX_UINT64;
    UINT64 max_addr = 0;
    BOOLEAN found_load = FALSE;
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS alloc_base;
    UINTN alloc_pages;

    if (image == NULL || entry_out == NULL || alloc_base_out == NULL || alloc_pages_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    if (!is_elf64_kernel(image, image_size)) {
        return EFI_UNSUPPORTED;
    }

    ehdr = (const elf64_ehdr_t *)image;
    if (ehdr->e_phentsize != sizeof(elf64_phdr_t)) {
        return EFI_UNSUPPORTED;
    }

    if (ehdr->e_phoff > image_size) {
        return EFI_LOAD_ERROR;
    }

    if ((UINT64)ehdr->e_phnum > (UINT64)((image_size - (UINTN)ehdr->e_phoff) / sizeof(elf64_phdr_t))) {
        return EFI_LOAD_ERROR;
    }

    phdrs = (const elf64_phdr_t *)(image + ehdr->e_phoff);

    for (UINTN i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = &phdrs[i];
        UINT64 seg_addr;
        UINT64 seg_start;
        UINT64 seg_end;

        if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0) {
            continue;
        }

        if (phdr->p_filesz > phdr->p_memsz) {
            return EFI_LOAD_ERROR;
        }

        if (phdr->p_offset > image_size || phdr->p_filesz > (UINT64)(image_size - (UINTN)phdr->p_offset)) {
            return EFI_LOAD_ERROR;
        }

        seg_addr = (phdr->p_paddr != 0) ? phdr->p_paddr : phdr->p_vaddr;
        if (seg_addr > KERNEL_PHYS_MAX) {
            return EFI_UNSUPPORTED;
        }

        if (phdr->p_memsz > ((KERNEL_PHYS_MAX + 1ULL) - seg_addr)) {
            return EFI_UNSUPPORTED;
        }

        seg_start = align_down_4k(seg_addr);
        Status = align_up_4k(seg_addr + phdr->p_memsz, &seg_end);
        if (EFI_ERROR(Status)) {
            return Status;
        }

        if (seg_start < min_addr) {
            min_addr = seg_start;
        }
        if (seg_end > max_addr) {
            max_addr = seg_end;
        }
        found_load = TRUE;
    }

    if (!found_load || max_addr <= min_addr) {
        return EFI_LOAD_ERROR;
    }

    alloc_pages = EFI_SIZE_TO_PAGES((UINTN)(max_addr - min_addr));
    alloc_base = (EFI_PHYSICAL_ADDRESS)min_addr;
    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, alloc_pages, &alloc_base);
    if (EFI_ERROR(Status)) {
        Print(L"AllocatePages for ELF kernel failed: %r\n", Status);
        return Status;
    }

    SetMem((VOID *)(UINTN)alloc_base, EFI_PAGES_TO_SIZE(alloc_pages), 0);

    for (UINTN i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t *phdr = &phdrs[i];
        UINT64 seg_addr;

        if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0 || phdr->p_filesz == 0) {
            continue;
        }

        seg_addr = (phdr->p_paddr != 0) ? phdr->p_paddr : phdr->p_vaddr;
        CopyMem((VOID *)(UINTN)seg_addr, image + phdr->p_offset, (UINTN)phdr->p_filesz);
    }

    if (ehdr->e_entry < min_addr || ehdr->e_entry >= max_addr || ehdr->e_entry > KERNEL_PHYS_MAX) {
        gBS->FreePages(alloc_base, alloc_pages);
        return EFI_LOAD_ERROR;
    }

    *entry_out = (EFI_PHYSICAL_ADDRESS)ehdr->e_entry;
    *alloc_base_out = alloc_base;
    *alloc_pages_out = alloc_pages;

    return EFI_SUCCESS;
}

static EFI_STATUS load_raw_kernel(const UINT8 *image,
                                  UINTN image_size,
                                  EFI_PHYSICAL_ADDRESS *entry_out,
                                  EFI_PHYSICAL_ADDRESS *alloc_base_out,
                                  UINTN *alloc_pages_out)
{
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS kernel_phys;
    UINTN pages;

    if (image == NULL || image_size == 0 || entry_out == NULL || alloc_base_out == NULL || alloc_pages_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    kernel_phys = (EFI_PHYSICAL_ADDRESS)KERNEL_LOAD_ADDR;
    pages = EFI_SIZE_TO_PAGES(image_size);

    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &kernel_phys);
    if (EFI_ERROR(Status)) {
        Print(L"AllocatePages at fixed kernel address failed: %r\n", Status);
        return Status;
    }

    SetMem((VOID *)(UINTN)kernel_phys, EFI_PAGES_TO_SIZE(pages), 0);
    CopyMem((VOID *)(UINTN)kernel_phys, image, image_size);

    *entry_out = kernel_phys;
    *alloc_base_out = kernel_phys;
    *alloc_pages_out = pages;

    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI exit_boot_services_and_prepare(EFI_HANDLE ImageHandle)
{
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN MapKey = 0;
    UINTN DescriptorSize = 0;
    UINTN MemMapSize = 0;
    UINTN MemMapCapacity = 0;
    UINT32 DescriptorVersion = 0;
    EFI_MEMORY_DESCRIPTOR *MemMap = NULL;

    for (UINTN attempt = 0; attempt < EXIT_BOOT_SERVICES_RETRY; attempt++) {
        if (MemMap == NULL) {
            MemMapSize = 0;
            Status = gBS->GetMemoryMap(&MemMapSize, NULL, &MapKey, &DescriptorSize, &DescriptorVersion);
            if (Status != EFI_BUFFER_TOO_SMALL) {
                Print(L"GetMemoryMap size query failed: %r\n", Status);
                return Status;
            }

            MemMapCapacity = MemMapSize + (DescriptorSize * MEMORY_MAP_EXTRA_DESC);
            MemMap = AllocatePool(MemMapCapacity);
            if (MemMap == NULL) {
                Print(L"AllocatePool failed for memory map\n");
                return EFI_OUT_OF_RESOURCES;
            }
        }

        MemMapSize = MemMapCapacity;
        Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            gBS->FreePool(MemMap);
            MemMap = NULL;
            MemMapCapacity = MemMapSize + (DescriptorSize * MEMORY_MAP_EXTRA_DESC);
            MemMap = AllocatePool(MemMapCapacity);
            if (MemMap == NULL) {
                Print(L"Re-AllocatePool failed for memory map\n");
                return EFI_OUT_OF_RESOURCES;
            }
            MemMapSize = MemMapCapacity;
            Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
        }

        if (EFI_ERROR(Status)) {
            Print(L"GetMemoryMap failed: %r\n", Status);
            gBS->FreePool(MemMap);
            return Status;
        }

        Status = gBS->ExitBootServices(ImageHandle, MapKey);
        if (!EFI_ERROR(Status)) {
            return EFI_SUCCESS;
        }

        if (Status != EFI_INVALID_PARAMETER) {
            Print(L"ExitBootServices failed: %r\n", Status);
            gBS->FreePool(MemMap);
            return Status;
        }
    }

    Print(L"ExitBootServices failed after retry: %r\n", Status);
    if (MemMap != NULL) {
        gBS->FreePool(MemMap);
    }
    return Status;
}

void EFIAPI uefi_init(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    gImageHandle = ImageHandle;

    gBS->SetWatchdogTimer(0, 0, 0, NULL);

    SystemTable->ConOut->SetAttribute(
        SystemTable->ConOut,
        EFI_BACKGROUND_BLUE | EFI_WHITE
    );
}

EFI_GRAPHICS_OUTPUT_PROTOCOL *EFIAPI init_gui(VOID)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    Status = gBS->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID **)&Gop);
    if (EFI_ERROR(Status)) {
        return NULL;
    }

    /* Prefer BGRX/RGBX modes compatible with the kernel framebuffer code */
    UINT32 best = Gop->Mode->Mode;
    UINTN bestPixels = 0;
    int bestRank = -1;

    for (UINT32 i = 0; i < Gop->Mode->MaxMode; i++)
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN size = 0;

        if (Gop->QueryMode(Gop, i, &size, &info) == EFI_SUCCESS)
        {
            uint32_t format = detect_framebuffer_format(info);
            int rank = framebuffer_format_rank(format);
            UINTN pixels = info->HorizontalResolution * info->VerticalResolution;

            if (rank > bestRank || (rank == bestRank && pixels > bestPixels))
            {
                bestRank = rank;
                bestPixels = pixels;
                best = i;
            }
            gBS->FreePool(info);
        }
    }

    if (bestRank <= 0) {
        Print(L"No compatible GOP mode found (need 32-bit BGRX/RGBX)\n");
        return NULL;
    }

    Status = Gop->SetMode(Gop, best);
    if (EFI_ERROR(Status)) {
        return NULL;
    }

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white = {255, 255, 255, 0};
    Gop->Blt(
        Gop,
        &white,
        EfiBltVideoFill,
        0, 0, 0, 0,
        Gop->Mode->Info->HorizontalResolution,
        Gop->Mode->Info->VerticalResolution,
        0
    );

    return Gop;
}

EFI_STATUS EFIAPI load_os(EFI_HANDLE ImageHandle, EFI_PHYSICAL_ADDRESS *entry_out)
{
    EFI_STATUS Status = EFI_SUCCESS;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *kernel = NULL;
    UINT8 *kernel_image = NULL;
    UINTN kernel_size = 0;
    EFI_PHYSICAL_ADDRESS loaded_base = 0;
    UINTN loaded_pages = 0;
    BOOLEAN pages_allocated = FALSE;
    const CHAR16 *selected_path = NULL;

    if (entry_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *entry_out = (EFI_PHYSICAL_ADDRESS)KERNEL_LOAD_ADDR;

    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&loaded_image);
    if (EFI_ERROR(Status) || loaded_image == NULL) {
        Print(L"HandleProtocol(LoadedImage) failed: %r\n", Status);
        goto cleanup;
    }

    Status = gBS->HandleProtocol(loaded_image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs);
    if (EFI_ERROR(Status) || fs == NULL) {
        /* fallback for firmware with unusual device handle wiring */
        Status = gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID **)&fs);
    }
    if (EFI_ERROR(Status) || fs == NULL) {
        Print(L"SimpleFileSystem protocol not found: %r\n", Status);
        goto cleanup;
    }

    Status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(Status)) {
        Print(L"OpenVolume failed: %r\n", Status);
        goto cleanup;
    }

    Status = open_kernel_file(root, &kernel, &selected_path);
    if (EFI_ERROR(Status)) {
        Print(L"Kernel file not found on boot volume\n");
        Status = EFI_NOT_FOUND;
        goto cleanup;
    }

    Status = read_file_contents(kernel, &kernel_image, &kernel_size);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to read kernel file (%s): %r\n", selected_path, Status);
        goto cleanup;
    }

    if (is_elf64_kernel(kernel_image, kernel_size)) {
        Print(L"Loading ELF64 kernel from: %s\n", selected_path);
        Status = load_elf_kernel(kernel_image, kernel_size, entry_out, &loaded_base, &loaded_pages);
    } else {
        Print(L"Loading flat binary kernel from: %s\n", selected_path);
        Status = load_raw_kernel(kernel_image, kernel_size, entry_out, &loaded_base, &loaded_pages);
    }

    if (EFI_ERROR(Status)) {
        goto cleanup;
    }

    pages_allocated = TRUE;
    Print(L"Kernel loaded: base=%p pages=%lu entry=%p\n",
          (VOID *)(UINTN)loaded_base,
          loaded_pages,
          (VOID *)(UINTN)(*entry_out));

cleanup:
    if (EFI_ERROR(Status) && pages_allocated) {
        gBS->FreePages(loaded_base, loaded_pages);
    }
    if (kernel_image != NULL) {
        FreePool(kernel_image);
    }
    close_if_open(&kernel);
    close_if_open(&root);
    return Status;
}
