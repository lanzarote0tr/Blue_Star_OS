#ifndef UEFIUTIL_H
#define UEFIUTIL_H

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI exit_boot_services_and_prepare(EFI_HANDLE ImageHandle);
void EFIAPI uefi_init(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable);
void EFIAPI load_os();
EFI_GRAPHICS_OUTPUT_PROTOCOL* EFIAPI init_gui();

#endif