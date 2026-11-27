/*
 * CookieOS Server 2026
 * CookieBoot - UEFI bootloader
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <efi.h>
#include <efilib.h>
#include "../include/elf_loader.h"

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

static INTN ascii_strcmp(const CHAR8 *A, const CHAR8 *B)
{
    while (*A && *B && *A == *B) {
        ++A;
        ++B;
    }

    return (INTN)((UINT8)*A - (UINT8)*B);
}

static BOOLEAN ascii_strncmp(
    const CHAR8 *A,
    const CHAR8 *B,
    UINTN Length
) {
    UINTN i;

    for (i = 0; i < Length; ++i) {
        if (A[i] != B[i])
            return FALSE;

        if (A[i] == '\0')
            return TRUE;
    }

    return TRUE;
}

/*
 * Open the filesystem containing CookieBoot.
 */
static EFI_STATUS open_root(
    EFI_HANDLE ImageHandle,
    EFI_FILE_HANDLE *Root
) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;

    Print(L"[DEBUG] Getting loaded image protocol...\r\n");

    Status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        ImageHandle,
        &LoadedImageProtocol,
        (VOID **)&LoadedImage
    );

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Loaded image protocol: %r\r\n", Status);
        return Status;
    }

    if (LoadedImage == NULL) {
        Print(L"[ERROR] Loaded image protocol returned NULL.\r\n");
        return EFI_DEVICE_ERROR;
    }

    Print(
        L"[DEBUG] Device handle: %p\r\n",
        LoadedImage->DeviceHandle
    );

    Print(L"[DEBUG] Getting filesystem protocol...\r\n");

    Status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        LoadedImage->DeviceHandle,
        &FileSystemProtocol,
        (VOID **)&FileSystem
    );

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Filesystem protocol: %r\r\n", Status);
        return Status;
    }

    if (FileSystem == NULL) {
        Print(L"[ERROR] Filesystem protocol returned NULL.\r\n");
        return EFI_DEVICE_ERROR;
    }

    Print(L"[DEBUG] Opening filesystem volume...\r\n");

    Status = uefi_call_wrapper(
        FileSystem->OpenVolume,
        2,
        FileSystem,
        Root
    );

    Print(L"[DEBUG] OpenVolume returned: %r\r\n", Status);

    return Status;
}

/*
 * Remove whitespace from both ends of an ASCII string.
 *
 * Every string passed here is NUL-terminated.
 */
static CHAR8 *trim(CHAR8 *Text)
{
    CHAR8 *End;

    while (*Text == ' ' || *Text == '\t')
        ++Text;

    End = Text;

    while (*End != '\0')
        ++End;

    while (End > Text &&
           (End[-1] == ' ' ||
            End[-1] == '\t' ||
            End[-1] == '\r' ||
            End[-1] == '\n')) {
        --End;
    }

    *End = '\0';

    return Text;
}

/*
 * Convert an ASCII string into a CHAR16 string.
 */
static EFI_STATUS copy_ascii_to_char16(
    CHAR16 *Destination,
    UINTN Capacity,
    const CHAR8 *Source
) {
    UINTN i = 0;

    if (Destination == NULL || Source == NULL || Capacity == 0)
        return EFI_INVALID_PARAMETER;

    while (Source[i] != '\0') {
        if (i + 1 >= Capacity)
            return EFI_BAD_BUFFER_SIZE;

        if ((UINT8)Source[i] > 0x7f)
            return EFI_UNSUPPORTED;

        Destination[i] = (CHAR16)(UINT8)Source[i];
        ++i;
    }

    Destination[i] = L'\0';

    return EFI_SUCCESS;
}

/*
 * Parse the timeout value.
 *
 * Accepted range: 0-300 seconds.
 */
static BOOLEAN parse_timeout(
    const CHAR8 *Text,
    UINTN *Result
) {
    UINTN Value = 0;
    UINTN i = 0;

    if (Text == NULL || Result == NULL || *Text == '\0')
        return FALSE;

    while (Text[i] >= '0' && Text[i] <= '9') {
        UINTN Digit = (UINTN)(Text[i] - '0');

        if (Value > 30 ||
            (Value == 30 && Digit > 0)) {
            return FALSE;
        }

        Value = Value * 10 + Digit;

        if (Value > 300)
            return FALSE;

        ++i;
    }

    if (Text[i] != '\0' || i == 0)
        return FALSE;

    *Result = Value;

    return TRUE;
}

/*
 * Process one configuration line.
 */
static EFI_STATUS parse_config_line(
    CHAR8 *Line,
    BOOT_CONFIG *Config,
    BOOT_ENTRY **CurrentEntry,
    BOOLEAN *InBootSection,
    UINTN LineNumber
) {
    EFI_STATUS Status;
    CHAR8 *Value;
    CHAR8 *Key;
    CHAR8 *Setting;

    Line = trim(Line);

    Print(
        L"[DEBUG] Line %u: %a\r\n",
        LineNumber,
        Line
    );

    if (*Line == '\0' || *Line == ';' || *Line == '#')
        return EFI_SUCCESS;

    /*
     * Section header.
     */
    if (*Line == '[') {
        CHAR8 *Close = Line;

        while (*Close != '\0' && *Close != ']')
            ++Close;

        *CurrentEntry = NULL;
        *InBootSection = FALSE;

        if (*Close != ']') {
            Print(
                L"[ERROR] Line %u: unterminated section.\r\n",
                LineNumber
            );

            return EFI_INVALID_PARAMETER;
        }

        *Close = '\0';

        {
            CHAR8 *Section = trim(Line + 1);

            if (ascii_strcmp(Section, "boot") == 0) {
                *InBootSection = TRUE;

                Print(L"[DEBUG] Found [boot].\r\n");

            } else if (ascii_strncmp(Section, "entry=", 6)) {
                CHAR8 *Name = trim(Section + 6);

                if (*Name == '\0') {
                    Print(
                        L"[ERROR] Line %u: empty entry name.\r\n",
                        LineNumber
                    );

                    return EFI_INVALID_PARAMETER;
                }

                if (Config->EntryCount >= MAX_ENTRIES) {
                    Print(L"[ERROR] Maximum boot entries exceeded.\r\n");
                    return EFI_OUT_OF_RESOURCES;
                }

                *CurrentEntry = &Config->Entries[Config->EntryCount];

                Status = copy_ascii_to_char16(
                    (*CurrentEntry)->Name,
                    ENTRY_NAME_SIZE,
                    Name
                );

                if (EFI_ERROR(Status)) {
                    Print(
                        L"[ERROR] Line %u: invalid entry name.\r\n",
                        LineNumber
                    );

                    return Status;
                }

                Status = copy_ascii_to_char16(
                    (*CurrentEntry)->Title,
                    ENTRY_TITLE_SIZE,
                    Name
                );

                if (EFI_ERROR(Status))
                    return Status;

                ++Config->EntryCount;

                Print(
                    L"[DEBUG] Created entry: %s\r\n",
                    (*CurrentEntry)->Name
                );
            } else {
                Print(
                    L"[DEBUG] Ignoring unknown section: %a\r\n",
                    Section
                );
            }
        }

        return EFI_SUCCESS;
    }

    /*
     * Find the equals sign.
     */
    Value = Line;

    while (*Value != '\0' && *Value != '=')
        ++Value;

    if (*Value != '=') {
        Print(
            L"[DEBUG] Ignoring line %u without '='.\r\n",
            LineNumber
        );

        return EFI_SUCCESS;
    }

    *Value++ = '\0';

    Key = trim(Line);
    Setting = trim(Value);

    /*
     * [boot] settings.
     */
    if (*InBootSection) {
        if (ascii_strcmp(Key, "default") == 0) {
            Status = copy_ascii_to_char16(
                Config->DefaultEntry,
                ENTRY_NAME_SIZE,
                Setting
            );

            if (EFI_ERROR(Status))
                return Status;

            Print(
                L"[DEBUG] Default entry: %s\r\n",
                Config->DefaultEntry
            );

        } else if (ascii_strcmp(Key, "timeout") == 0) {
            UINTN Timeout;

            if (parse_timeout(Setting, &Timeout)) {
                Config->Timeout = Timeout;
            } else {
                Print(
                    L"[ERROR] Line %u: invalid timeout.\r\n",
                    LineNumber
                );

                return EFI_INVALID_PARAMETER;
            }

            Print(
                L"[DEBUG] Timeout: %u\r\n",
                Config->Timeout
            );
        }

        return EFI_SUCCESS;
    }

    /*
     * [entry=name] settings.
     */
    if (*CurrentEntry != NULL) {
        if (ascii_strcmp(Key, "title") == 0) {
            Status = copy_ascii_to_char16(
                (*CurrentEntry)->Title,
                ENTRY_TITLE_SIZE,
                Setting
            );

            if (EFI_ERROR(Status))
                return Status;

            Print(
                L"[DEBUG] Entry title: %s\r\n",
                (*CurrentEntry)->Title
            );

        } else if (ascii_strcmp(Key, "kernel") == 0) {
            if (*Setting != '\\') {
                Print(
                    L"[ERROR] Line %u: kernel path must start with '\\'.\r\n",
                    LineNumber
                );

                return EFI_INVALID_PARAMETER;
            }

            Status = copy_ascii_to_char16(
                (*CurrentEntry)->Kernel,
                KERNEL_PATH_SIZE,
                Setting
            );

            if (EFI_ERROR(Status))
                return Status;

            (*CurrentEntry)->HasKernel = TRUE;

            Print(
                L"[DEBUG] Kernel path: %s\r\n",
                (*CurrentEntry)->Kernel
            );
        }
    }

    return EFI_SUCCESS;
}

/*
 * Read and parse \boot.ini.
 *
 * Supports:
 *   LF
 *   CRLF
 *   CR
 */
static EFI_STATUS read_config(
    EFI_FILE_HANDLE Root,
    BOOT_CONFIG *Config
) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE File = NULL;

    CHAR8 Buffer[CONFIG_MAX_SIZE + 1];

    UINTN ReadSize = sizeof(Buffer);
    UINTN LineNumber = 0;

    CHAR8 *Line;
    CHAR8 *End;

    BOOT_ENTRY *CurrentEntry = NULL;
    BOOLEAN InBootSection = FALSE;

    Print(L"[DEBUG] read_config entered.\r\n");

    SetMem(Config, sizeof(*Config), 0);

    Config->Timeout = 5;

    /*
     * Open boot.ini.
     */
    Print(L"[DEBUG] Opening %s...\r\n", BOOT_INI_PATH);

    Status = uefi_call_wrapper(
        Root->Open,
        5,
        Root,
        &File,
        BOOT_INI_PATH,
        EFI_FILE_MODE_READ,
        0
    );

    Print(L"[DEBUG] Root->Open returned: %r\r\n", Status);

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] Cannot open \\boot.ini: %r\r\n", Status);
        return Status;
    }

    if (File == NULL) {
        Print(L"[ERROR] NULL file handle.\r\n");
        return EFI_DEVICE_ERROR;
    }

    /*
     * Read boot.ini.
     */
    Print(L"[DEBUG] boot.ini handle: %p\r\n", File);
    Print(L"[DEBUG] Reading boot.ini...\r\n");

    Status = uefi_call_wrapper(
        File->Read,
        3,
        File,
        &ReadSize,
        Buffer
    );

    Print(L"[DEBUG] File->Read returned: %r\r\n", Status);
    Print(L"[DEBUG] Bytes read: %u\r\n", ReadSize);

    uefi_call_wrapper(File->Close, 1, File);

    Print(L"[DEBUG] boot.ini closed.\r\n");

    if (EFI_ERROR(Status))
        return Status;

    /*
     * Reject oversized configuration files.
     */
    if (ReadSize > CONFIG_MAX_SIZE) {
        Print(L"[ERROR] boot.ini exceeds 4096 bytes.\r\n");
        return EFI_BAD_BUFFER_SIZE;
    }

    Buffer[ReadSize] = '\0';

    Line = Buffer;
    End = Buffer + ReadSize;

    Print(L"[DEBUG] Starting INI parsing...\r\n");

    /*
     * Parse each line using bounded pointer arithmetic.
     */
    while (Line < End) {
        CHAR8 *NextLine = Line;
        CHAR8 Separator;

        ++LineNumber;

        /*
         * Find the next line ending.
         */
        while (NextLine < End &&
               *NextLine != '\r' &&
               *NextLine != '\n') {
            ++NextLine;
        }

        /*
         * Terminate this line and advance past its separator.
         */
        if (NextLine < End) {
            Separator = *NextLine;
            *NextLine = '\0';
            ++NextLine;

            if (Separator == '\r' &&
                NextLine < End &&
                *NextLine == '\n') {
                ++NextLine;
            }
        }

        /*
         * Process the line.
         */
        Status = parse_config_line(
            Line,
            Config,
            &CurrentEntry,
            &InBootSection,
            LineNumber
        );

        if (EFI_ERROR(Status)) {
            Print(
                L"[ERROR] Failed parsing line %u: %r\r\n",
                LineNumber,
                Status
            );

            return Status;
        }

        /*
         * Verify forward progress.
         */
        if (NextLine <= Line && NextLine < End) {
            Print(L"[ERROR] Parser did not advance.\r\n");
            return EFI_COMPROMISED_DATA;
        }

        Line = NextLine;
    }

    Print(L"[DEBUG] INI parsing completed.\r\n");

    /*
     * Validate entries.
     */
    if (Config->EntryCount == 0) {
        Print(L"[ERROR] No [entry=...] sections found.\r\n");
        return EFI_NOT_FOUND;
    }

    {
        UINTN i;

        for (i = 0; i < Config->EntryCount; ++i) {
            if (!Config->Entries[i].HasKernel) {
                Print(
                    L"[ERROR] Entry %s has no kernel setting.\r\n",
                    Config->Entries[i].Name
                );

                return EFI_INVALID_PARAMETER;
            }
        }
    }

    Print(
        L"[DEBUG] Parsed %u boot entries successfully.\r\n",
        Config->EntryCount
    );

    return EFI_SUCCESS;
}

/*
 * Find the configured default entry.
 */
static UINTN find_default_entry(const BOOT_CONFIG *Config)
{
    UINTN i;

    for (i = 0; i < Config->EntryCount; ++i) {
        if (StrCmp(
            Config->Entries[i].Name,
            Config->DefaultEntry
        ) == 0) {
            return i;
        }
    }

    return 0;
}

/*
 * Display the boot menu.
 */
static EFI_STATUS show_boot_menu(
    const BOOT_CONFIG *Config,
    UINTN *Selected
) {
    EFI_STATUS Status = EFI_SUCCESS;

    EFI_EVENT TimerEvent = NULL;
    EFI_EVENT Events[2];

    UINTN EventIndex;
    UINTN Current = find_default_entry(Config);
    UINTN Remaining = Config->Timeout;

    Print(L"\r\nCookieOS Server 2026\r\n");
    Print(L"CookieBoot - Select an operating system\r\n");
    Print(L"----------------------------------------\r\n");

    if (Config->Timeout == 0) {
        Print(
            L"[BOOT] Timeout is zero; selecting %s.\r\n",
            Config->Entries[Current].Title
        );

        *Selected = Current;
        return EFI_SUCCESS;
    }

    /*
     * Create timer event.
     */
    Status = uefi_call_wrapper(
        BS->CreateEvent,
        5,
        EVT_TIMER,
        TPL_CALLBACK,
        NULL,
        NULL,
        &TimerEvent
    );

    if (EFI_ERROR(Status)) {
        Print(L"[ERROR] CreateEvent failed: %r\r\n", Status);
        return Status;
    }

    while (Remaining > 0) {
        UINTN i;

        Print(L"\r\n");

        for (i = 0; i < Config->EntryCount; ++i) {
            Print(
                i == Current ? L"  > %s\r\n" : L"    %s\r\n",
                Config->Entries[i].Title
            );
        }

        Print(
            L"\r\nUse UP/DOWN and ENTER. Booting in %u seconds...\r\n",
            Remaining
        );

        /*
         * Arm the timer for one second.
         */
        Status = uefi_call_wrapper(
            BS->SetTimer,
            3,
            TimerEvent,
            TimerRelative,
            10000000ULL
        );

        if (EFI_ERROR(Status)) {
            Print(L"[ERROR] SetTimer failed: %r\r\n", Status);
            break;
        }

        Events[0] = ST->ConIn->WaitForKey;
        Events[1] = TimerEvent;

        /*
         * Wait for keyboard or timer.
         */
        Status = uefi_call_wrapper(
            BS->WaitForEvent,
            3,
            2,
            Events,
            &EventIndex
        );

        if (EFI_ERROR(Status)) {
            Print(L"[ERROR] WaitForEvent failed: %r\r\n", Status);
            break;
        }

        if (EventIndex == 1) {
            --Remaining;
            continue;
        }

        /*
         * Process keyboard input.
         */
        {
            EFI_INPUT_KEY Key;

            Status = uefi_call_wrapper(
                ST->ConIn->ReadKeyStroke,
                2,
                ST->ConIn,
                &Key
            );

            if (EFI_ERROR(Status))
                continue;

            if (Key.ScanCode == SCAN_UP) {
                Current = Current == 0
                    ? Config->EntryCount - 1
                    : Current - 1;

            } else if (Key.ScanCode == SCAN_DOWN) {
                Current = (Current + 1) % Config->EntryCount;

            } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
                break;
            }
        }
    }

    /*
     * Destroy timer event.
     */
    uefi_call_wrapper(
        BS->SetTimer,
        3,
        TimerEvent,
        TimerCancel,
        0
    );

    uefi_call_wrapper(
        BS->CloseEvent,
        1,
        TimerEvent
    );

    if (EFI_ERROR(Status))
        return Status;

    *Selected = Current;

    return EFI_SUCCESS;
}

/*
 * CookieBoot entry point.
 *
 * GNU-EFI's startup code invokes efi_main using its
 * application entry convention. Do not add EFIAPI here.
 */
EFI_STATUS efi_main(
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

    /*
     * Open filesystem.
     */
    Print(L"[DEBUG] Opening filesystem...\r\n");

    Status = open_root(ImageHandle, &Root);

    if (EFI_ERROR(Status)) {
        Print(
            L"[ERROR] Could not open EFI filesystem: %r\r\n",
            Status
        );

        return Status;
    }

    /*
     * Read boot.ini.
     */
    Print(L"[BOOT] Reading \\boot.ini...\r\n");

    Status = read_config(Root, &Config);

    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Root->Close, 1, Root);

        Print(
            L"[ERROR] Configuration loading failed: %r\r\n",
            Status
        );

        return Status;
    }

    Print(L"[BOOT] Configuration loaded successfully.\r\n");

    /*
     * Show boot menu.
     */
    Print(L"[BOOT] Starting boot menu...\r\n");

    Status = show_boot_menu(&Config, &Selected);

    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Root->Close, 1, Root);

        Print(
            L"[ERROR] Boot menu failed: %r\r\n",
            Status
        );

        return Status;
    }

    /*
     * Transfer control to the selected ELF64 kernel.
     * The ELF loader exits boot services and passes COOKIEOS_BOOT_INFO
     * in RDI at the kernel entry point.
     */
    uefi_call_wrapper(Root->Close, 1, Root);

    return load_kernel_and_exit(
        ImageHandle,
        SystemTable,
        Config.Entries[Selected].Kernel
    );
}
