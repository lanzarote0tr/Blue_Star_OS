#ifndef UEFIUTIL_H
#define UEFIUTIL_H

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI exit_boot_services_and_prepare(EFI_HANDLE ImageHandle);
void EFIAPI uefi_init(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable);
EFI_STATUS EFIAPI load_os(EFI_HANDLE ImageHandle, EFI_PHYSICAL_ADDRESS *entry_out);
EFI_GRAPHICS_OUTPUT_PROTOCOL *EFIAPI init_gui(VOID);

#endif
