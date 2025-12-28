#include <Uefi.h>

#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>

#include <Guid/FileInfo.h>
#include "../bootinfo.h"


EFI_STATUS EFIAPI exit_boot_services_and_prepare(EFI_HANDLE ImageHandle)
{
    EFI_STATUS Status;
    UINTN MapKey, DescriptorSize, MemMapSize = 0;
    UINT32 DescriptorVersion;
    EFI_MEMORY_DESCRIPTOR *MemMap = NULL;

    Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL)
    {
        Print(L"GetMemoryMap failed (expected BUFFER_TOO_SMALL): %r\n", Status);
        return Status;
    }

    MemMapSize += 2 * DescriptorSize;
    MemMap = AllocatePool(MemMapSize);
    if (!MemMap)
    {
        Print(L"AllocatePool failed\n");
        return EFI_OUT_OF_RESOURCES;
    }

    Status = gBS->GetMemoryMap(&MemMapSize, MemMap, &MapKey, &DescriptorSize, &DescriptorVersion);
    if (EFI_ERROR(Status))
    {
        Print(L"GetMemoryMap failed: %r\n", Status);
        return Status;
    }

    Status = gBS->ExitBootServices(ImageHandle, MapKey);
    if (EFI_ERROR(Status))
    {
        Print(L"ExitBootServices failed: %r\n", Status);
        return Status;
    }
    return EFI_SUCCESS;
}

void EFIAPI uefi_init(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable){
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    gImageHandle = ImageHandle;

        SystemTable->ConOut->SetAttribute(
        SystemTable->ConOut,
        EFI_BACKGROUND_BLUE | EFI_WHITE
    );
}

EFI_GRAPHICS_OUTPUT_PROTOCOL* EFIAPI init_gui()
{
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    Status = gBS->LocateProtocol(
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID **)&Gop);
    if (EFI_ERROR(Status))
    {
        while (1)
            __asm__ __volatile__("hlt");
    }

    // 가장 큰 해상도 선택
    UINT32 best = Gop->Mode->Mode;
    UINTN bestPixels = 0;

    for (UINT32 i = 0; i < Gop->Mode->MaxMode; i++)
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        UINTN size;

        if (Gop->QueryMode(Gop, i, &size, &info) == EFI_SUCCESS)
        {
            UINTN pixels = info->HorizontalResolution * info->VerticalResolution;
            if (pixels > bestPixels)
            {
                bestPixels = pixels;
                best = i;
            }
        }
    }

    Status = Gop->SetMode(Gop, best);
    if (EFI_ERROR(Status))
    {
        while (1)
            __asm__ __volatile__("hlt");
    }

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white = {255, 255, 255, 0};

    Gop->Blt( Gop, &white, EfiBltVideoFill, 0, 0, 0, 0, Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution, 0);

    return Gop;
}

VOID EFIAPI load_os(VOID)
{
    EFI_STATUS Status;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *kernel;

    Status = gBS->LocateProtocol(
        &gEfiSimpleFileSystemProtocolGuid,
        NULL,
        (VOID **)&fs
    );
    if (EFI_ERROR(Status)) return;

    Status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(Status)) return;

    Status = root->Open(
        root,
        &kernel,
        L"kernel",
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(Status)) {
        Print(L"No such file.");
    }

    // --- 파일 정보 얻기 ---
    EFI_FILE_INFO *info;
    UINTN info_size = 0;

    // 1차 호출: size만 얻기
    kernel->GetInfo(kernel, &gEfiFileInfoGuid, &info_size, NULL);

    // size만큼 재할당
    gBS->AllocatePool(EfiLoaderData, info_size, (VOID **)&info);

    // 2차 호출: 실제 정보 받기
    kernel->GetInfo(kernel, &gEfiFileInfoGuid, &info_size, info);

    UINTN kernel_size = info->FileSize;

    // --- 커널 로드할 물리 주소 ---
    EFI_PHYSICAL_ADDRESS kernel_phys = KERNEL_LOAD_ADDRESS;

    Status = gBS->AllocatePages(
        AllocateAddress,
        EfiLoaderData,
        EFI_SIZE_TO_PAGES(kernel_size),
        &kernel_phys
    );
    if (EFI_ERROR(Status)) return;

    // --- 파일 읽기 ---
    Status = kernel->Read(
        kernel,
        &kernel_size,
        (VOID *)(UINTN)kernel_phys
    );
    if (EFI_ERROR(Status)) return;

    // 여기까지:
    // kernel.elf 가 물리주소 0x200000에 그대로 로드됨
}
