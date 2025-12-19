#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS exit_boot_services_and_prepare(EFI_HANDLE ImageHandle)
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