/*
 * CookieOS Server 2026
 * CookieBoot - UEFI bootloader
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <efi.h>
#include <efilib.h>

#define BOOT_INI_PATH L"\\boot.ini"
#define CONFIG_MAX_SIZE 4096
#define MAX_ENTRIES 16
#define ENTRY_NAME_SIZE 64
#define ENTRY_TITLE_SIZE 96
#define KERNEL_PATH_SIZE 260

typedef struct {
    CHAR16 Name[ENTRY_NAME_SIZE];
    CHAR16 Title[ENTRY_TITLE_SIZE];
    CHAR16 Kernel[KERNEL_PATH_SIZE];
    BOOLEAN HasKernel;
} BOOT_ENTRY;

typedef struct {
    BOOT_ENTRY Entries[MAX_ENTRIES];
    UINTN EntryCount;
    CHAR16 DefaultEntry[ENTRY_NAME_SIZE];
    UINTN Timeout;
} BOOT_CONFIG;

static EFI_STATUS open_root(
    EFI_HANDLE ImageHandle,
    EFI_FILE_HANDLE *Root
) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;

    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3, ImageHandle,
        &LoadedImageProtocol, (void **)&LoadedImage
    );
    if (EFI_ERROR(Status))
        return Status;

    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3, LoadedImage->DeviceHandle,
        &FileSystemProtocol, (void **)&FileSystem
    );
    if (EFI_ERROR(Status))
        return Status;

    return uefi_call_wrapper(
        FileSystem->OpenVolume, 2, FileSystem, Root
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
           (End[-1] == ' ' || End[-1] == '\t'))
        --End;

    *End = '\0';
    return Text;
}

static EFI_STATUS copy_ascii_to_char16(
    CHAR16 *Destination,
    UINTN Capacity,
    const CHAR8 *Source
) {
    UINTN i = 0;

    while (Source[i] != '\0') {
        if (i + 1 >= Capacity || (UINT8)Source[i] > 0x7f)
            return EFI_BAD_BUFFER_SIZE;

        Destination[i] = (CHAR16)(UINT8)Source[i];
        ++i;
    }

    Destination[i] = L'\0';
    return EFI_SUCCESS;
}

static EFI_STATUS read_config(
    EFI_FILE_HANDLE Root,
    BOOT_CONFIG *Config
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE File = NULL;
    CHAR8 Buffer[CONFIG_MAX_SIZE + 1];
    UINTN ReadSize = CONFIG_MAX_SIZE + 1;
    CHAR8 *Line;
    BOOT_ENTRY *CurrentEntry = NULL;
    BOOLEAN InBootSection = FALSE;

    SetMem(Config, sizeof(*Config), 0);
    Config->Timeout = 5;

    Status = uefi_call_wrapper(
        Root->Open, 5, Root, &File,
        BOOT_INI_PATH, EFI_FILE_MODE_READ, 0
    );
    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Cannot open \\boot.ini: 0x%lx\r\n", Status);
        return Status;
    }

    Status = uefi_call_wrapper(
        File->Read, 3, File, &ReadSize, Buffer
    );
    uefi_call_wrapper(File->Close, 1, File);

    if (EFI_ERROR(Status))
        return Status;

    if (ReadSize > CONFIG_MAX_SIZE) {
        Print(L"[ERROR] boot.ini exceeds %u bytes.\r\n",
              (UINTN)CONFIG_MAX_SIZE);
        return EFI_BAD_BUFFER_SIZE;
    }

    Buffer[ReadSize] = '\0';
    Line = Buffer;

    while (*Line != '\0') {
        CHAR8 *NextLine = Line;
        CHAR8 *Value;

        while (*NextLine != '\0' &&
               *NextLine != '\r' && *NextLine != '\n')
            ++NextLine;

        if (*NextLine != '\0') {
            CHAR8 Separator = *NextLine++;
            if (Separator == '\r' && *NextLine == '\n')
                ++NextLine;
            NextLine[-1] = '\0';
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

            CurrentEntry = NULL;
            InBootSection = FALSE;

            if (*Close == ']') {
                *Close = '\0';
                CHAR8 *Section = trim(Line + 1);

                if (AsciiStrCmp(Section, "boot") == 0) {
                    InBootSection = TRUE;
                } else if (AsciiStrnCmp(Section, "entry=", 6) == 0) {
                    CHAR8 *Name = trim(Section + 6);

                    if (*Name == '\0' ||
                        Config->EntryCount >= MAX_ENTRIES) {
                        Print(L"[ERROR] Invalid or excessive boot entries.\r\n");
                        return EFI_INVALID_PARAMETER;
                    }

                    CurrentEntry = &Config->Entries[Config->EntryCount];
                    Status = copy_ascii_to_char16(
                        CurrentEntry->Name, ENTRY_NAME_SIZE, Name
                    );
                    if (EFI_ERROR(Status))
                        return Status;

                    Status = copy_ascii_to_char16(
                        CurrentEntry->Title, ENTRY_TITLE_SIZE, Name
                    );
                    if (EFI_ERROR(Status))
                        return Status;

                    ++Config->EntryCount;
                }
            }

            Line = NextLine;
            continue;
        }

        Value = Line;
        while (*Value != '\0' && *Value != '=')
            ++Value;

        if (*Value != '=') {
            Line = NextLine;
            continue;
        }

        *Value++ = '\0';
        CHAR8 *Key = trim(Line);
        CHAR8 *Setting = trim(Value);

        if (InBootSection) {
            if (AsciiStrCmp(Key, "default") == 0) {
                Status = copy_ascii_to_char16(
                    Config->DefaultEntry, ENTRY_NAME_SIZE, Setting
                );
                if (EFI_ERROR(Status))
                    return Status;
            } else if (AsciiStrCmp(Key, "timeout") == 0) {
                UINTN Timeout = 0;
                UINTN i = 0;

                if (*Setting == '\0') {
                    Line = NextLine;
                    continue;
                }

                while (Setting[i] >= '0' && Setting[i] <= '9') {
                    Timeout = Timeout * 10 + (UINTN)(Setting[i] - '0');
                    if (Timeout > 300)
                        break;
                    ++i;
                }

                if (Setting[i] == '\0' && i != 0 && Timeout <= 300)
                    Config->Timeout = Timeout;
            }
        } else if (CurrentEntry != NULL) {
            if (AsciiStrCmp(Key, "title") == 0) {
                Status = copy_ascii_to_char16(
                    CurrentEntry->Title, ENTRY_TITLE_SIZE, Setting
                );
                if (EFI_ERROR(Status))
                    return Status;
            } else if (AsciiStrCmp(Key, "kernel") == 0) {
                if (*Setting != '\\') {
                    Print(L"[ERROR] Entry kernel path must start with \\\\.\r\n");
                    return EFI_INVALID_PARAMETER;
                }

                Status = copy_ascii_to_char16(
                    CurrentEntry->Kernel, KERNEL_PATH_SIZE, Setting
                );
                if (EFI_ERROR(Status))
                    return Status;

                CurrentEntry->HasKernel = TRUE;
            }
        }

        Line = NextLine;
    }

    if (Config->EntryCount == 0) {
        Print(L"[ERROR] No [entry=...] sections found in boot.ini.\r\n");
        return EFI_NOT_FOUND;
    }

    {
        UINTN i;
        for (i = 0; i < Config->EntryCount; ++i) {
            if (!Config->Entries[i].HasKernel) {
                Print(L"[ERROR] Entry %s has no kernel= setting.\r\n",
                      Config->Entries[i].Name);
                return EFI_INVALID_PARAMETER;
            }
        }
    }

    return EFI_SUCCESS;
}

static UINTN find_default_entry(const BOOT_CONFIG *Config)
{
    UINTN i;

    for (i = 0; i < Config->EntryCount; ++i) {
        if (StrCmp(Config->Entries[i].Name, Config->DefaultEntry) == 0)
            return i;
    }

    return 0;
}

static EFI_STATUS show_boot_menu(
    const BOOT_CONFIG *Config,
    UINTN *Selected
) {
    EFI_STATUS Status;
    EFI_EVENT TimerEvent = NULL;
    EFI_EVENT Events[2];
    UINTN EventIndex;
    UINTN Current = find_default_entry(Config);
    UINTN Remaining = Config->Timeout;

    Print(L"\r\nCookieOS Server 2026\r\n");
    Print(L"CookieBoot - Select an operating system\r\n");
    Print(L"----------------------------------------\r\n");

    if (Config->Timeout == 0) {
        Print(L"[BOOT] Timeout is zero; selecting %s.\r\n",
              Config->Entries[Current].Title);
        *Selected = Current;
        return EFI_SUCCESS;
    }

    Status = uefi_call_wrapper(
        BS->CreateEvent, 5,
        EVT_TIMER, 0, NULL, NULL, &TimerEvent
    );
    if (EFI_ERROR(Status))
        return Status;

    while (Remaining > 0) {
        UINTN i;

        Print(L"\r\n");
        for (i = 0; i < Config->EntryCount; ++i) {
            Print(i == Current ? L"  > %s\r\n" : L"    %s\r\n",
                  Config->Entries[i].Title);
        }

        Print(L"\r\nUse UP/DOWN and ENTER. Booting default in %u seconds...\r\n",
              Remaining);

        Status = uefi_call_wrapper(
            BS->SetTimer, 3,
            TimerEvent, TimerRelative, 10000000ULL
        );
        if (EFI_ERROR(Status))
            break;

        Events[0] = ST->ConIn->WaitForKey;
        Events[1] = TimerEvent;

        Status = uefi_call_wrapper(
            BS->WaitForEvent, 3, 2, Events, &EventIndex
        );
        if (EFI_ERROR(Status))
            break;

        if (EventIndex == 1) {
            --Remaining;
            continue;
        }

        {
            EFI_INPUT_KEY Key;
            Status = uefi_call_wrapper(
                ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key
            );
            if (EFI_ERROR(Status))
                continue;

            if (Key.ScanCode == SCAN_UP) {
                Current = (Current == 0)
                    ? Config->EntryCount - 1 : Current - 1;
            } else if (Key.ScanCode == SCAN_DOWN) {
                Current = (Current + 1) % Config->EntryCount;
            } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
                break;
            }
        }

        Print(L"\r\n");
    }

    uefi_call_wrapper(BS->SetTimer, 3, TimerEvent, TimerCancel, 0);
    uefi_call_wrapper(BS->CloseEvent, 1, TimerEvent);

    if (EFI_ERROR(Status))
        return Status;

    *Selected = Current;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE Root = NULL;
    EFI_FILE_HANDLE KernelFile = NULL;
    BOOT_CONFIG Config;
    UINTN Selected;

    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\nCookieOS Server 2026\r\n");
    Print(L"CookieBoot UEFI Loader\r\n");
    Print(L"--------------------------\r\n");
    Print(L"[BOOT] Initializing CookieBoot...\r\n");

    Status = open_root(ImageHandle, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Could not open EFI filesystem: 0x%lx\r\n", Status);
        return Status;
    }

    Print(L"[BOOT] Reading \\boot.ini...\r\n");
    Status = read_config(Root, &Config);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }

    Status = show_boot_menu(&Config, &Selected);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Root->Close, 1, Root);
        Print(L"[ERROR] Boot menu failed: 0x%lx\r\n", Status);
        return Status;
    }

    Print(L"[BOOT] Selected: %s\r\n", Config.Entries[Selected].Title);
    Print(L"[BOOT] Opening %s...\r\n", Config.Entries[Selected].Kernel);

    Status = uefi_call_wrapper(
        Root->Open, 5, Root, &KernelFile,
        Config.Entries[Selected].Kernel, EFI_FILE_MODE_READ, 0
    );

    uefi_call_wrapper(Root->Close, 1, Root);

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Could not open selected kernel: 0x%lx\r\n", Status);
        return Status;
    }

    Print(L"[OK] Selected kernel file found!\r\n");
    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);

    Print(L"[BOOT] Kernel loading is not implemented yet.\r\n");
    return EFI_SUCCESS;
}
