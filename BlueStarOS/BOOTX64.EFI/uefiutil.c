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

EFI_STATUS EFIAPI load_os(VOID)
{
    EFI_STATUS Status;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *kernel = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = 0;

    // 1. 파일 시스템 프로토콜 가져오기
    Status = gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&fs);
    if (EFI_ERROR(Status)) {
        Print(L"LocateProtocol failed: %r\n", Status);
        return Status;
    }

    // 2. ESP 루트 열기
    Status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(Status)) {
        Print(L"OpenVolume failed: %r\n", Status);
        return Status;
    }

    // 3. kernel 파일 위치 후보
    CHAR16 *candidates[] = {
        L"\\kernel",
        L"\\EFI\\BOOT\\KERNEL",
    };

    UINTN i;
    for (i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        Status = root->Open(root, &kernel, candidates[i], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(Status)) break; // 성공하면 루프 종료
    }

    if (EFI_ERROR(Status) || kernel == NULL) {
        Print(L"No such file in any known location.\n");
        if (root) root->Close(root);
        return EFI_NOT_FOUND;
    }

    Print(L"Kernel found at: %s\n", candidates[i]);

    // --- 파일 정보(크기) 얻기: 먼저 크기 질의 ---
    info_size = 0;
    Status = kernel->GetInfo(kernel, &gEfiFileInfoGuid, &info_size, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL) {
        Print(L"GetInfo (size query) failed: %r\n", Status);
        kernel->Close(kernel);
        root->Close(root);
        return Status;
    }

    Status = gBS->AllocatePool(EfiLoaderData, info_size, (VOID**)&info);
    if (EFI_ERROR(Status)) {
        Print(L"AllocatePool for FileInfo failed: %r\n", Status);
        kernel->Close(kernel);
        root->Close(root);
        return Status;
    }

    Status = kernel->GetInfo(kernel, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(Status)) {
        Print(L"GetInfo (read) failed: %r\n", Status);
        gBS->FreePool(info);
        kernel->Close(kernel);
        root->Close(root);
        return Status;
    }

    UINTN kernel_size = (UINTN)info->FileSize;
    if (kernel_size == 0) {
        Print(L"Kernel file size is 0\n");
        gBS->FreePool(info);
        kernel->Close(kernel);
        root->Close(root);
        return EFI_INVALID_PARAMETER;
    }

    // --- 페이지 수 계산 및 메모리 할당 ---
    UINTN pages = EFI_SIZE_TO_PAGES(kernel_size);
    EFI_PHYSICAL_ADDRESS kernel_phys = 0;

    // (옵션) 고정 주소에 로드하려면 아래처럼 시도할 수 있지만 실패할 가능성이 있으므로
    // 먼저 고정 주소를 시도해보고 실패하면 AnyPages로 대체하는 방식으로 작성함.
#ifdef KERNEL_LOAD_ADDR
    kernel_phys = (EFI_PHYSICAL_ADDRESS)KERNEL_LOAD_ADDR;
    Status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &kernel_phys);
    if (EFI_ERROR(Status)) {
        // 고정주소 할당 실패하면 아무 주소에나 할당
        kernel_phys = 0;
        Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &kernel_phys);
    }
#else
    Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &kernel_phys);
#endif
    if (EFI_ERROR(Status)) {
        Print(L"AllocatePages failed: %r\n", Status);
        gBS->FreePool(info);
        kernel->Close(kernel);
        root->Close(root);
        return Status;
    }

    // --- 파일 포인터를 파일 시작으로 맞춘 뒤 읽기 ---
    Status = kernel->SetPosition(kernel, 0);
    if (EFI_ERROR(Status)) {
        Print(L"SetPosition failed: %r\n", Status);
        gBS->FreePages(kernel_phys, pages);
        gBS->FreePool(info);
        kernel->Close(kernel);
        root->Close(root);
        return Status;
    }

    UINTN bytes_to_read = kernel_size;
    Status = kernel->Read(kernel, &bytes_to_read, (VOID*)(UINTN)kernel_phys);
    if (EFI_ERROR(Status) || bytes_to_read != kernel_size) {
        Print(L"Read failed: %r, read %u/%u\n", Status, bytes_to_read, kernel_size);
        gBS->FreePages(kernel_phys, pages);
        gBS->FreePool(info);
        kernel->Close(kernel);
        root->Close(root);
        return EFI_LOAD_ERROR;
    }

    Print(L"Kernel loaded to physical address %p, size %u bytes\n", (VOID*)(UINTN)kernel_phys, kernel_size);

    // 정리
    gBS->FreePool(info);
    kernel->Close(kernel);
    root->Close(root);

    return EFI_SUCCESS;
}
