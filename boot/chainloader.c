/*
 * CookieOS Server 2026
 * BOOTX64.EFI - UEFI fallback chainloader
 *
 * Loads:
 *   \cookieos\System\Boot\CookieBoot.efi
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <efi.h>
#include <efilib.h>

#define COOKIEBOOT_PATH L"\\cookieos\\System\\Boot\\CookieBoot.efi"

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage = NULL;
    EFI_DEVICE_PATH *DevicePath = NULL;
    EFI_HANDLE CookieBootImage = NULL;

    /*
     * Print through the firmware console before initializing GNU-EFI.
     * This confirms that the firmware reached the application entry.
     */
    if (SystemTable != NULL && SystemTable->ConOut != NULL) {
        uefi_call_wrapper(
            SystemTable->ConOut->OutputString,
            2,
            SystemTable->ConOut,
            L"BOOTX64: UEFI entry point reached.\r\n"
        );
    }

    InitializeLib(ImageHandle, SystemTable);

    Print(L"CookieOS Server 2026\r\n");
    Print(L"BOOTX64.EFI: Starting CookieOS chainloader...\r\n");

    Status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        ImageHandle,
        &LoadedImageProtocol,
        (VOID **)&LoadedImage
    );

    if (EFI_ERROR(Status)) {
        Print(L"BOOTX64: Cannot get loaded image protocol: %r\r\n", Status);
        return Status;
    }

    DevicePath = FileDevicePath(
        LoadedImage->DeviceHandle,
        COOKIEBOOT_PATH
    );

    if (DevicePath == NULL) {
        Print(L"BOOTX64: Cannot construct CookieBoot device path.\r\n");
        return EFI_OUT_OF_RESOURCES;
    }

    Print(L"BOOTX64: Loading %s\r\n", COOKIEBOOT_PATH);

    Status = uefi_call_wrapper(
        BS->LoadImage,
        6,
        FALSE,
        ImageHandle,
        DevicePath,
        NULL,
        0,
        &CookieBootImage
    );

    FreePool(DevicePath);

    if (EFI_ERROR(Status)) {
        Print(L"BOOTX64: LoadImage failed: %r\r\n", Status);
        return Status;
    }

    Print(L"BOOTX64: CookieBoot loaded successfully.\r\n");
    Print(L"BOOTX64: Starting CookieBoot...\r\n");

    Status = uefi_call_wrapper(
        BS->StartImage,
        3,
        CookieBootImage,
        NULL,
        NULL
    );

    Print(L"BOOTX64: CookieBoot returned: %r\r\n", Status);

    uefi_call_wrapper(
        BS->UnloadImage,
        1,
        CookieBootImage
    );

    return EFI_ERROR(Status) ? Status : EFI_LOAD_ERROR;
}
