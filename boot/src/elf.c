/*
 * CookieOS Server 2026 - ELF64 kernel loader
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#include "../include/bootinfo.h"
#include "../include/elf_loader.h"

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PAGE_BYTES 4096
#define MEMORY_MAP_SLACK 8192

typedef struct {
    UINT8 ident[16];
    UINT16 type;
    UINT16 machine;
    UINT32 version;
    UINT64 entry;
    UINT64 phoff;
    UINT64 shoff;
    UINT32 flags;
    UINT16 ehsize;
    UINT16 phentsize;
    UINT16 phnum;
    UINT16 shentsize;
    UINT16 shnum;
    UINT16 shstrndx;
} ELF64_EHDR;

typedef struct {
    UINT32 type;
    UINT32 flags;
    UINT64 offset;
    UINT64 vaddr;
    UINT64 paddr;
    UINT64 filesz;
    UINT64 memsz;
    UINT64 align;
} ELF64_PHDR;

typedef struct {
    UINT64 start;
    UINT64 end;
    UINT64 physical;
    UINT64 pages;
    BOOLEAN executable;
} LOADED_SEGMENT;

static BOOLEAN range_valid(UINT64 offset, UINT64 length, UINT64 total)
{
    return offset <= total && length <= total - offset;
}

static BOOLEAN add_overflow_u64(UINT64 a, UINT64 b, UINT64 *result)
{
    *result = a + b;
    return *result < a;
}

static EFI_STATUS read_entire_file(
    EFI_FILE_HANDLE Root,
    const CHAR16 *Path,
    VOID **Data,
    UINTN *DataSize
)
{
    EFI_STATUS Status;
    EFI_FILE_HANDLE File = NULL;
    EFI_FILE_INFO *Info = NULL;
    EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
    UINTN InfoSize = SIZE_OF_EFI_FILE_INFO + 256;
    UINTN ReadSize;

    Status = uefi_call_wrapper(Root->Open, 5, Root, &File, Path,
                               EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
        return Status;

    Status = uefi_call_wrapper(BS->AllocatePool, 3,
                               EfiLoaderData, InfoSize, (VOID **)&Info);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    Status = uefi_call_wrapper(File->GetInfo, 4, File,
                               &FileInfoGuid, &InfoSize, Info);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(BS->FreePool, 1, Info);
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    if (Info->FileSize == 0 || Info->FileSize > 0xFFFFFFFFULL) {
        uefi_call_wrapper(BS->FreePool, 1, Info);
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_BAD_BUFFER_SIZE;
    }

    *DataSize = (UINTN)Info->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, Info);

    Status = uefi_call_wrapper(BS->AllocatePool, 3,
                               EfiLoaderData, *DataSize, Data);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    ReadSize = *DataSize;
    Status = uefi_call_wrapper(File->Read, 3, File, &ReadSize, *Data);
    uefi_call_wrapper(File->Close, 1, File);

    if (EFI_ERROR(Status) || ReadSize != *DataSize) {
        uefi_call_wrapper(BS->FreePool, 1, *Data);
        *Data = NULL;
        return EFI_ERROR(Status) ? Status : EFI_END_OF_FILE;
    }

    return EFI_SUCCESS;
}

static EFI_STATUS load_elf_segments(
    const UINT8 *Image,
    UINTN ImageSize,
    const ELF64_EHDR *Header,
    LOADED_SEGMENT *Segments,
    UINTN *SegmentCount,
    UINT64 *KernelBase,
    UINT64 *KernelEnd,
    UINT64 *PhysicalEntry
)
{
    UINTN i;
    UINTN count = 0;
    UINT64 lowest = ~0ULL;
    UINT64 highest = 0;
    BOOLEAN entry_found = FALSE;

    if (Header->phentsize != sizeof(ELF64_PHDR) ||
        Header->phnum == 0 || Header->phnum > 128)
        return EFI_LOAD_ERROR;

    if (!range_valid(Header->phoff,
                     (UINT64)Header->phnum * Header->phentsize,
                     ImageSize))
        return EFI_LOAD_ERROR;

    for (i = 0; i < Header->phnum; ++i) {
        const ELF64_PHDR *ph =
            (const ELF64_PHDR *)(Image + Header->phoff +
                                 (UINT64)i * Header->phentsize);
        EFI_PHYSICAL_ADDRESS address;
        UINT64 load_address;
        UINT64 page_start;
        UINT64 page_end;
        UINT64 pages;
        UINT64 segment_end;
        UINT64 virtual_end;
        EFI_STATUS Status;
        UINTN j;

        if (ph->type != PT_LOAD || ph->memsz == 0)
            continue;

        if (ph->filesz > ph->memsz ||
            !range_valid(ph->offset, ph->filesz, ImageSize))
            return EFI_LOAD_ERROR;

        load_address = ph->paddr ? ph->paddr : ph->vaddr;

        if (add_overflow_u64(load_address, ph->memsz, &segment_end) ||
            add_overflow_u64(ph->vaddr, ph->memsz, &virtual_end))
            return EFI_LOAD_ERROR;

        if (load_address == 0 || segment_end <= load_address)
            return EFI_LOAD_ERROR;

        page_start = load_address & ~((UINT64)PAGE_BYTES - 1);
        if (segment_end > (~0ULL - (PAGE_BYTES - 1)))
            return EFI_LOAD_ERROR;
        page_end = (segment_end + PAGE_BYTES - 1) &
                   ~((UINT64)PAGE_BYTES - 1);
        pages = (page_end - page_start) / PAGE_BYTES;

        if (count >= 128)
            return EFI_OUT_OF_RESOURCES;

        /* Separate PT_LOAD segments must not claim the same pages. */
        for (j = 0; j < count; ++j) {
            UINT64 old_start = Segments[j].physical;
            UINT64 old_end = old_start + Segments[j].pages * PAGE_BYTES;
            if (page_start < old_end && page_end > old_start)
                return EFI_LOAD_ERROR;
        }

        address = page_start;
        Status = uefi_call_wrapper(BS->AllocatePages, 4,
                                   AllocateAddress, EfiLoaderData,
                                   pages, &address);
        if (EFI_ERROR(Status)) {
            Print(L"[ELF] AllocatePages failed at %lx: %r\r\n",
                  page_start, Status);
            return Status;
        }

        SetMem((VOID *)(UINTN)page_start, (UINTN)(pages * PAGE_BYTES), 0);
        CopyMem((VOID *)(UINTN)load_address,
                (CONST VOID *)(Image + ph->offset),
                (UINTN)ph->filesz);

        Segments[count].start = load_address;
        Segments[count].end = segment_end;
        Segments[count].physical = page_start;
        Segments[count].pages = pages;
        Segments[count].executable = (ph->flags & PF_X) != 0;

        if (page_start < lowest)
            lowest = page_start;
        if (page_end > highest)
            highest = page_end;

        /*
         * ELF e_entry is a virtual address. Translate it through the
         * matching PT_LOAD segment to the physical address we loaded.
         */
        if (Header->entry >= ph->vaddr &&
            Header->entry < virtual_end &&
            Segments[count].executable) {
            *PhysicalEntry = load_address +
                             (Header->entry - ph->vaddr);
            entry_found = TRUE;
        }

        ++count;
    }

    if (count == 0 || !entry_found)
        return EFI_LOAD_ERROR;

    *SegmentCount = count;
    *KernelBase = lowest;
    *KernelEnd = highest;
    return EFI_SUCCESS;
}

static UINT64 find_acpi_rsdp(EFI_SYSTEM_TABLE *SystemTable)
{
    UINTN i;
    EFI_GUID Acpi20Guid = ACPI_20_TABLE_GUID;
    EFI_GUID AcpiGuid = ACPI_TABLE_GUID;

    for (i = 0; i < SystemTable->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *table = &SystemTable->ConfigurationTable[i];

        if (CompareGuid(&table->VendorGuid, &Acpi20Guid) ||
            CompareGuid(&table->VendorGuid, &AcpiGuid))
            return (UINT64)(UINTN)table->VendorTable;
    }

    return 0;
}

static VOID collect_framebuffer(COOKIEOS_BOOT_INFO *Info)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Mode;

    Status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &GraphicsOutputProtocol, NULL,
                               (VOID **)&Gop);
    if (EFI_ERROR(Status) || Gop == NULL || Gop->Mode == NULL ||
        Gop->Mode->Info == NULL)
        return;

    Mode = Gop->Mode->Info;
    Info->framebuffer_base = Gop->Mode->FrameBufferBase;
    Info->framebuffer_size = Gop->Mode->FrameBufferSize;
    Info->framebuffer_width = Mode->HorizontalResolution;
    Info->framebuffer_height = Mode->VerticalResolution;
    Info->framebuffer_pixels_per_scanline = Mode->PixelsPerScanLine;
    Info->framebuffer_pixel_format = Mode->PixelFormat;
}

static EFI_STATUS exit_boot_services_and_jump(
    EFI_HANDLE ImageHandle,
    COOKIEOS_BOOT_INFO *Info,
    UINT64 Entry
)
{
    EFI_STATUS Status;
    EFI_MEMORY_DESCRIPTOR *Map = NULL;
    UINTN MapSize = 0;
    UINTN MapKey = 0;
    UINTN DescriptorSize = 0;
    UINT32 DescriptorVersion = 0;
    UINTN AllocationSize = 0;
    UINTN Attempt;

    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               65536, (VOID **)&Map);
    if (EFI_ERROR(Status))
        return Status;

    AllocationSize = 65536;

    for (Attempt = 0; Attempt < 4; ++Attempt) {
        MapSize = AllocationSize;

        Status = uefi_call_wrapper(BS->GetMemoryMap, 5,
                                   &MapSize, Map, &MapKey,
                                   &DescriptorSize, &DescriptorVersion);

        if (Status == EFI_BUFFER_TOO_SMALL) {
            uefi_call_wrapper(BS->FreePool, 1, Map);
            AllocationSize = MapSize + MEMORY_MAP_SLACK;
            Status = uefi_call_wrapper(BS->AllocatePool, 3,
                                       EfiLoaderData, AllocationSize,
                                       (VOID **)&Map);
            if (EFI_ERROR(Status))
                return Status;
            continue;
        }

        if (EFI_ERROR(Status))
            return Status;

        Info->memory_map = (UINT64)(UINTN)Map;
        Info->memory_map_size = MapSize;
        Info->memory_descriptor_size = DescriptorSize;
        Info->memory_descriptor_version = DescriptorVersion;

        Status = uefi_call_wrapper(BS->ExitBootServices, 2,
                                   ImageHandle, MapKey);
        if (!EFI_ERROR(Status))
            break;

        if (Status != EFI_INVALID_PARAMETER)
            return Status;
    }

    if (EFI_ERROR(Status))
        return Status;

    /*
     * UEFI boot services are gone. Enter the kernel with BootInfo in RDI.
     * This jump uses the physical entry address; the kernel starts with
     * firmware page tables still active and must establish its own mappings.
     */
    __asm__ __volatile__(
        "jmp *%%rax"
        :
        : "D"(Info), "a"(Entry)
        : "memory"
    );

    __builtin_unreachable();
}

EFI_STATUS load_kernel_and_exit(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable,
    const CHAR16 *Path
)
{
    EFI_STATUS Status;
    EFI_FILE_HANDLE Root = NULL;
    VOID *Image = NULL;
    UINTN ImageSize = 0;
    ELF64_EHDR *Header;
    LOADED_SEGMENT Segments[128];
    UINTN SegmentCount = 0;
    UINT64 KernelBase = 0;
    UINT64 KernelEnd = 0;
    UINT64 PhysicalEntry = 0;
    COOKIEOS_BOOT_INFO *Info = NULL;

    EFI_LOADED_IMAGE *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;

    Print(L"[ELF] Opening kernel: %s\r\n", Path);

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle,
                               &LoadedImageProtocol, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status))
        return Status;

    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               LoadedImage->DeviceHandle,
                               &FileSystemProtocol, (VOID **)&FileSystem);
    if (EFI_ERROR(Status))
        return Status;

    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2,
                               FileSystem, &Root);
    if (EFI_ERROR(Status))
        return Status;

    Status = read_entire_file(Root, Path, &Image, &ImageSize);
    uefi_call_wrapper(Root->Close, 1, Root);
    if (EFI_ERROR(Status))
        return Status;

    if (ImageSize < sizeof(ELF64_EHDR)) {
        Status = EFI_LOAD_ERROR;
        goto fail;
    }

    Header = (ELF64_EHDR *)Image;

    if (Header->ident[0] != 0x7f ||
        Header->ident[1] != 'E' ||
        Header->ident[2] != 'L' ||
        Header->ident[3] != 'F' ||
        Header->ident[4] != ELFCLASS64 ||
        Header->ident[5] != ELFDATA2LSB ||
        Header->ident[6] != EV_CURRENT ||
        Header->type != ET_EXEC ||
        Header->machine != EM_X86_64 ||
        Header->version != EV_CURRENT ||
        Header->ehsize != sizeof(ELF64_EHDR)) {
        Print(L"[ELF] Invalid ELF64 x86_64 executable.\r\n");
        Status = EFI_LOAD_ERROR;
        goto fail;
    }

    Print(L"[ELF] ELF64 executable validated.\r\n");

    Status = load_elf_segments((const UINT8 *)Image, ImageSize,
                               Header, Segments, &SegmentCount,
                               &KernelBase, &KernelEnd, &PhysicalEntry);
    if (EFI_ERROR(Status)) {
        Print(L"[ELF] Segment loading failed: %r\r\n", Status);
        goto fail;
    }

    Print(L"[ELF] Loaded %u PT_LOAD segments.\r\n", SegmentCount);
    Print(L"[ELF] Physical entry: %lx\r\n", PhysicalEntry);

    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               sizeof(*Info), (VOID **)&Info);
    if (EFI_ERROR(Status))
        goto fail;

    SetMem(Info, sizeof(*Info), 0);
    Info->magic = COOKIEOS_BOOTINFO_MAGIC;
    Info->version = COOKIEOS_BOOTINFO_VERSION;
    Info->size = sizeof(*Info);
    Info->acpi_rsdp = find_acpi_rsdp(SystemTable);
    Info->kernel_physical_base = KernelBase;
    Info->kernel_size = KernelEnd - KernelBase;
    Info->kernel_entry = PhysicalEntry;
    collect_framebuffer(Info);

    Print(L"[ELF] Exiting UEFI boot services...\r\n");

    Status = exit_boot_services_and_jump(ImageHandle, Info, PhysicalEntry);

fail:
    if (Image != NULL)
        uefi_call_wrapper(BS->FreePool, 1, Image);

    return Status;
}
