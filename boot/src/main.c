#include <efi.h>
#include <efilib.h>

#define MT_KERNEL_PATH L"\\cookieos\\System\\mtoskrnl.ckx"

static EFI_STATUS open_kernel(
    EFI_HANDLE ImageHandle,
    EFI_FILE_HANDLE *KernelFile
) {
    EFI_STATUS Status;

    EFI_LOADED_IMAGE *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
    EFI_FILE_HANDLE Root = NULL;

    Status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        ImageHandle,
        &LoadedImageProtocol,
        (void **)&LoadedImage
    );

    if (EFI_ERROR(Status))
        return Status;

    Status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        LoadedImage->DeviceHandle,
        &FileSystemProtocol,
        (void **)&FileSystem
    );

    if (EFI_ERROR(Status))
        return Status;

    Status = uefi_call_wrapper(
        FileSystem->OpenVolume,
        2,
        FileSystem,
        &Root
    );

    if (EFI_ERROR(Status))
        return Status;

    Status = uefi_call_wrapper(
        Root->Open,
        5,
        Root,
        KernelFile,
        MT_KERNEL_PATH,
        EFI_FILE_MODE_READ,
        0
    );

    uefi_call_wrapper(Root->Close, 1, Root);

    return Status;
}

EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE KernelFile = NULL;

    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\n");
    Print(L"CookieOS Server 2026\r\n");
    Print(L"CookieBoot UEFI Loader\r\n");
    Print(L"--------------------------\r\n");

    Print(L"[BOOT] Initializing CookieBoot...\r\n");

    Print(L"[BOOT] Locating MT kernel...\r\n");

    Status = open_kernel(ImageHandle, &KernelFile);

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Could not open mtoskrnl.ckx\r\n");
        Print(L"[ERROR] EFI status: 0x%lx\r\n", Status);

        return Status;
    }

    Print(L"[OK] MT kernel found!\r\n");

    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);

    Print(L"[BOOT] Kernel file access test complete.\r\n");
    Print(L"[BOOT] CookieBoot is operational.\r\n");

    return EFI_SUCCESS;
}
