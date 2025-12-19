#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_GRAPHICS_OUTPUT_PROTOCOL* init_gui(){ 
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *GOP;
    Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (void **)&GOP);
    if (EFI_ERROR(Status))
    {
        for (;;)
            __asm__ volatile("hlt");
    }

    GOP->SetMode(GOP, 0);

    return GOP;

}