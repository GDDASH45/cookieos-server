/*
 * CookieOS Server 2026
 * CookieBoot - UEFI bootloader
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <efi.h>
#include <efilib.h>

#define BOOT_INI_PATH L"\\boot.ini"
#define CONFIG_SIZE 4096
#define KERNEL_PATH_SIZE 260

static EFI_STATUS open_root(
    EFI_HANDLE ImageHandle,
    EFI_FILE_HANDLE *Root
) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;

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

    return uefi_call_wrapper(
        FileSystem->OpenVolume,
        2,
        FileSystem,
        Root
    );
}

static CHAR8 *trim(CHAR8 *Text)
{
    CHAR8 *End;

    while (*Text == ' ' || *Text == '\t')
        ++Text;

    End = Text;
    while (*End != '\0')
        ++End;

    while (End > Text &&
           (End[-1] == ' ' || End[-1] == '\t' ||
            End[-1] == '\r' || End[-1] == '\n')) {
        --End;
    }

    *End = '\0';
    return Text;
}

static EFI_STATUS read_kernel_path(
    EFI_FILE_HANDLE Root,
    CHAR16 *KernelPath,
    UINTN KernelPathCapacity
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE ConfigFile = NULL;
    CHAR8 Config[CONFIG_SIZE];
    UINTN ReadSize = sizeof(Config) - 1;
    CHAR8 *Line;
    CHAR8 *NextLine;
    BOOLEAN InBootSection = FALSE;
    BOOLEAN FoundKernel = FALSE;

    Status = uefi_call_wrapper(
        Root->Open,
        5,
        Root,
        &ConfigFile,
        BOOT_INI_PATH,
        EFI_FILE_MODE_READ,
        0
    );

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Could not open \\boot.ini: 0x%lx\r\n", Status);
        return Status;
    }

    Status = uefi_call_wrapper(
        ConfigFile->Read,
        3,
        ConfigFile,
        &ReadSize,
        Config
    );

    uefi_call_wrapper(ConfigFile->Close, 1, ConfigFile);

    if (EFI_ERROR(Status))
        return Status;

    if (ReadSize == sizeof(Config) - 1) {
        Print(L"[ERROR] boot.ini is too large (maximum %u bytes).\r\n",
              (UINTN)(sizeof(Config) - 1));
        return EFI_BAD_BUFFER_SIZE;
    }

    Config[ReadSize] = '\0';

    Line = Config;
    while (*Line != '\0') {
        CHAR8 *Value;

        NextLine = Line;
        while (*NextLine != '\0' &&
               *NextLine != '\r' && *NextLine != '\n') {
            ++NextLine;
        }

        if (*NextLine != '\0') {
            CHAR8 Separator = *NextLine;
            *NextLine++ = '\0';

            if (Separator == '\r' && *NextLine == '\n')
                ++NextLine;
        }

        Line = trim(Line);

        if (*Line == '\0' || *Line == ';' || *Line == '#') {
            Line = NextLine;
            continue;
        }

        if (*Line == '[') {
            CHAR8 *Close = Line;
            while (*Close != '\0' && *Close != ']')
                ++Close;

            if (*Close == ']') {
                *Close = '\0';
                InBootSection = (AsciiStrCmp(trim(Line + 1), "boot") == 0);
            } else {
                InBootSection = FALSE;
            }

            Line = NextLine;
            continue;
        }

        if (InBootSection) {
            Value = Line;
            while (*Value != '\0' && *Value != '=')
                ++Value;

            if (*Value == '=') {
                *Value++ = '\0';

                if (AsciiStrCmp(trim(Line), "kernel") == 0) {
                    CHAR8 *Path = trim(Value);
                    UINTN i = 0;

                    if (*Path != '\\') {
                        Print(L"[ERROR] boot.ini kernel path must begin with \\\\.\r\n");
                        return EFI_INVALID_PARAMETER;
                    }

                    while (Path[i] != '\0') {
                        if (i + 1 >= KernelPathCapacity ||
                            (UINT8)Path[i] > 0x7f) {
                            Print(L"[ERROR] Invalid or oversized kernel path.\r\n");
                            return EFI_BAD_BUFFER_SIZE;
                        }

                        KernelPath[i] = (CHAR16)(UINT8)Path[i];
                        ++i;
                    }

                    KernelPath[i] = L'\0';
                    FoundKernel = TRUE;
                    break;
                }
            }
        }

        Line = NextLine;
    }

    if (!FoundKernel) {
        Print(L"[ERROR] No kernel= entry found in [boot].\r\n");
        return EFI_NOT_FOUND;
    }

    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE Root = NULL;
    EFI_FILE_HANDLE KernelFile = NULL;
    CHAR16 KernelPath[KERNEL_PATH_SIZE];

    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\n");
    Print(L"CookieOS Server 2026\r\n");
    Print(L"CookieBoot UEFI Loader\r\n");
    Print(L"--------------------------\r\n");

    Print(L"[BOOT] Initializing CookieBoot...\r\n");

    Status = open_root(ImageHandle, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Could not open EFI filesystem: 0x%lx\r\n", Status);
        return Status;
    }

    Print(L"[BOOT] Reading \\boot.ini...\r\n");

    Status = read_kernel_path(Root, KernelPath, KERNEL_PATH_SIZE);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }

    Print(L"[OK] Kernel path: %s\r\n", KernelPath);
    Print(L"[BOOT] Opening MT kernel...\r\n");

    Status = uefi_call_wrapper(
        Root->Open,
        5,
        Root,
        &KernelFile,
        KernelPath,
        EFI_FILE_MODE_READ,
        0
    );

    uefi_call_wrapper(Root->Close, 1, Root);

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Could not open configured kernel: 0x%lx\r\n", Status);
        return Status;
    }

    Print(L"[OK] MT kernel found!\r\n");

    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);

    Print(L"[BOOT] Kernel file access test complete.\r\n");
    Print(L"[BOOT] CookieBoot is operational.\r\n");

    return EFI_SUCCESS;
}
