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

    InitializeLib(ImageHandle, SystemTable);

    Print(L"CookieOS Server 2026\r\n");
    Print(L"BOOTX64.EFI: Starting CookieOS chainloader...\r\n");

    /*
     * Get information about the currently running image.
     */
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

    /*
     * Construct the CookieBoot device path relative to the
     * filesystem device that launched BOOTX64.EFI.
     */
    DevicePath = FileDevicePath(
        LoadedImage->DeviceHandle,
        COOKIEBOOT_PATH
    );

    if (DevicePath == NULL) {
        Print(L"BOOTX64: Cannot construct CookieBoot device path.\r\n");
        return EFI_OUT_OF_RESOURCES;
    }

    Print(L"BOOTX64: Loading %s\r\n", COOKIEBOOT_PATH);

    /*
     * Ask UEFI to load CookieBoot. This validates and loads
     * the EFI executable, but does not execute it yet.
     */
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
    DevicePath = NULL;

    if (EFI_ERROR(Status)) {
        Print(L"BOOTX64: LoadImage failed: %r\r\n", Status);
        return Status;
    }

    Print(L"BOOTX64: CookieBoot loaded successfully.\r\n");
    Print(L"BOOTX64: Starting CookieBoot...\r\n");

    /*
     * Execute CookieBoot.
     */
    Status = uefi_call_wrapper(
        BS->StartImage,
        3,
        CookieBootImage,
        NULL,
        NULL
    );

    /*
     * StartImage only returns if CookieBoot exits or fails.
     * Clean up the child image in that case.
     */
    Print(L"BOOTX64: CookieBoot returned: %r\r\n", Status);

    uefi_call_wrapper(
        BS->UnloadImage,
        1,
        CookieBootImage
    );

    return EFI_ERROR(Status) ? Status : EFI_LOAD_ERROR;
}
