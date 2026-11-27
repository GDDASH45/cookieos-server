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
    const UINT8 *Image, UINTN ImageSize, const ELF64_EHDR *Header,
    LOADED_SEGMENT *Segments, UINTN *SegmentCount,
    UINT64 *KernelBase, UINT64 *KernelEnd, UINT64 *EntryPoint
)
{
    UINTN i, count = 0;
    UINT64 lowest = ~0ULL, highest = 0;
    BOOLEAN entry_found = FALSE;
    EFI_PHYSICAL_ADDRESS AllocationBase = 0;
    UINT64 allocation_size, allocation_pages;

    if (Header->phentsize != sizeof(ELF64_PHDR) ||
        Header->phnum == 0 || Header->phnum > 128 ||
        !range_valid(Header->phoff,
                     (UINT64)Header->phnum * Header->phentsize,
                     ImageSize))
        return EFI_LOAD_ERROR;

    for (i = 0; i < Header->phnum; ++i) {
        const ELF64_PHDR *ph =
            (const ELF64_PHDR *)(Image + Header->phoff +
                                 (UINT64)i * Header->phentsize);
        UINT64 segment_end, page_start, page_end;
        UINTN j;

        if (ph->type != PT_LOAD || ph->memsz == 0)
            continue;
        if (ph->filesz > ph->memsz ||
            !range_valid(ph->offset, ph->filesz, ImageSize))
            return EFI_LOAD_ERROR;
        if (ph->paddr != 0 && ph->paddr != ph->vaddr)
            return EFI_UNSUPPORTED;
        if (add_overflow_u64(ph->vaddr, ph->memsz, &segment_end) ||
            segment_end <= ph->vaddr ||
            segment_end > 0x8000000000ULL ||
            segment_end > (~0ULL - (PAGE_BYTES - 1)))
            return EFI_LOAD_ERROR;

        page_start = ph->vaddr & ~((UINT64)PAGE_BYTES - 1);
        page_end = (segment_end + PAGE_BYTES - 1) &
                   ~((UINT64)PAGE_BYTES - 1);
        if (count >= 128)
            return EFI_OUT_OF_RESOURCES;

        for (j = 0; j < count; ++j) {
            UINT64 old_start =
                Segments[j].start & ~((UINT64)PAGE_BYTES - 1);
            UINT64 old_end = old_start + Segments[j].pages * PAGE_BYTES;
            if (page_start < old_end && page_end > old_start)
                return EFI_LOAD_ERROR;
        }

        Segments[count].start = ph->vaddr;
        Segments[count].end = segment_end;
        Segments[count].physical = 0;
        Segments[count].pages = (page_end - page_start) / PAGE_BYTES;
        Segments[count].executable = (ph->flags & PF_X) != 0;

        if (page_start < lowest) lowest = page_start;
        if (page_end > highest) highest = page_end;
        if (Header->entry >= ph->vaddr &&
            Header->entry < segment_end &&
            Segments[count].executable)
            entry_found = TRUE;
        ++count;
    }

    if (count == 0 || !entry_found || highest <= lowest)
        return EFI_LOAD_ERROR;

    allocation_size = highest - lowest;
    allocation_pages = allocation_size / PAGE_BYTES;
    if (allocation_pages == 0 ||
        allocation_pages > (~(UINTN)0) / PAGE_BYTES)
        return EFI_OUT_OF_RESOURCES;

    /*
     * Allocate physical memory wherever UEFI has free pages. The kernel
     * remains linked at its original virtual address and is mapped there
     * before the entry point is called.
     */
    {
        EFI_STATUS Status = uefi_call_wrapper(
            BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData,
            (UINTN)allocation_pages, &AllocationBase
        );
        if (EFI_ERROR(Status)) {
            Print(L"[ELF] Could not allocate %lu kernel pages: %r\r\n",
                  allocation_pages, Status);
            return Status;
        }
    }

    if (AllocationBase >= 0x8000000000ULL ||
        allocation_size > 0x8000000000ULL - AllocationBase) {
        uefi_call_wrapper(BS->FreePages, 2,
                          AllocationBase, (UINTN)allocation_pages);
        return EFI_UNSUPPORTED;
    }

    SetMem((VOID *)(UINTN)AllocationBase,
           (UINTN)allocation_size, 0);

    count = 0;
    for (i = 0; i < Header->phnum; ++i) {
        const ELF64_PHDR *ph =
            (const ELF64_PHDR *)(Image + Header->phoff +
                                 (UINT64)i * Header->phentsize);
        UINT64 page_start, destination;

        if (ph->type != PT_LOAD || ph->memsz == 0)
            continue;
        page_start = ph->vaddr & ~((UINT64)PAGE_BYTES - 1);
        destination = AllocationBase + (ph->vaddr - lowest);
        if (ph->filesz != 0)
            CopyMem((VOID *)(UINTN)destination,
                    (CONST VOID *)(Image + ph->offset),
                    (UINTN)ph->filesz);

        Segments[count].physical = AllocationBase + (page_start - lowest);
        ++count;
    }

    *SegmentCount = count;
    *KernelBase = AllocationBase;
    *KernelEnd = AllocationBase + allocation_size;
    *EntryPoint = Header->entry;
    Print(L"[ELF] Kernel virtual range: %lx-%lx\r\n", lowest, highest);
    Print(L"[ELF] Kernel physical allocation: %lx-%lx\r\n",
          *KernelBase, *KernelEnd);
    return EFI_SUCCESS;
}

/*
 * Build temporary identity mappings for the initial kernel handoff.
 * One PML4, one PDPT, and 512 page directories map the lower 512 GiB
 * with 2 MiB pages. The kernel currently uses physical == virtual addresses.
 */
#define IDENTITY_PDPT_ENTRIES 512
#define IDENTITY_PAGE_TABLE_PAGES (2 + IDENTITY_PDPT_ENTRIES)

static EFI_STATUS map_kernel_page(
    UINT64 *Pdpt, UINT64 VirtualAddress, UINT64 PhysicalAddress
)
{
    UINTN PdptIndex = (UINTN)((VirtualAddress >> 30) & 0x1ff);
    UINTN PdIndex = (UINTN)((VirtualAddress >> 21) & 0x1ff);
    UINTN PtIndex = (UINTN)((VirtualAddress >> 12) & 0x1ff);
    UINT64 *Pd, *Pt, Pde;
    EFI_STATUS Status;

    if (VirtualAddress >= 0x8000000000ULL ||
        PhysicalAddress >= 0x8000000000ULL)
        return EFI_UNSUPPORTED;

    Pd = (UINT64 *)(UINTN)(Pdpt[PdptIndex] & ~0xfffULL);
    Pde = Pd[PdIndex];
    if ((Pde & 1ULL) == 0)
        return EFI_LOAD_ERROR;

    if ((Pde & 0x080ULL) != 0) {
        EFI_PHYSICAL_ADDRESS PtBase = 0;
        UINT64 BlockBase = VirtualAddress & ~0x1fffffULL;
        UINTN i;

        Status = uefi_call_wrapper(BS->AllocatePages, 4,
                                   AllocateAnyPages, EfiLoaderData,
                                   1, &PtBase);
        if (EFI_ERROR(Status))
            return Status;

        Pt = (UINT64 *)(UINTN)PtBase;
        for (i = 0; i < 512; ++i)
            Pt[i] = (BlockBase + (UINT64)i * PAGE_BYTES) | 0x003ULL;
        Pd[PdIndex] = PtBase | 0x003ULL;
    } else {
        Pt = (UINT64 *)(UINTN)(Pde & ~0xfffULL);
    }

    Pt[PtIndex] =
        (PhysicalAddress & ~((UINT64)PAGE_BYTES - 1)) | 0x003ULL;
    return EFI_SUCCESS;
}

static EFI_STATUS create_identity_page_tables(
    const LOADED_SEGMENT *Segments, UINTN SegmentCount,
    EFI_PHYSICAL_ADDRESS *PageTableBase
)
{
    EFI_STATUS Status;
    EFI_PHYSICAL_ADDRESS Base = 0;
    UINT64 *Pml4, *Pdpt;
    UINTN i, j;

    Status = uefi_call_wrapper(BS->AllocatePages, 4,
                               AllocateAnyPages, EfiLoaderData,
                               IDENTITY_PAGE_TABLE_PAGES, &Base);
    if (EFI_ERROR(Status))
        return Status;

    SetMem((VOID *)(UINTN)Base,
           IDENTITY_PAGE_TABLE_PAGES * PAGE_BYTES, 0);
    Pml4 = (UINT64 *)(UINTN)Base;
    Pdpt = (UINT64 *)(UINTN)(Base + PAGE_BYTES);
    Pml4[0] = (Base + PAGE_BYTES) | 0x003ULL;

    for (i = 0; i < IDENTITY_PDPT_ENTRIES; ++i) {
        UINT64 *Pd = (UINT64 *)(UINTN)
            (Base + (2 + i) * PAGE_BYTES);
        Pdpt[i] = (UINT64)(UINTN)Pd | 0x003ULL;
        for (j = 0; j < 512; ++j) {
            UINT64 Physical = ((UINT64)i * 512ULL + j) * 0x200000ULL;
            Pd[j] = Physical | 0x083ULL;
        }
    }

    for (i = 0; i < SegmentCount; ++i) {
        UINT64 VirtualPage =
            Segments[i].start & ~((UINT64)PAGE_BYTES - 1);
        for (j = 0; j < Segments[i].pages; ++j) {
            Status = map_kernel_page(
                Pdpt,
                VirtualPage + (UINT64)j * PAGE_BYTES,
                Segments[i].physical + (UINT64)j * PAGE_BYTES
            );
            if (EFI_ERROR(Status)) {
                Print(L"[ELF] Kernel page mapping failed: %r\r\n", Status);
                return Status;
            }
        }
    }

    *PageTableBase = Base;
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


/*
 * Display the optional 128x128 uncompressed BMP splash for three seconds.
 * The image is sent directly to GOP, so this runs before ExitBootServices().
 */
static UINT16 bmp_read_u16(const UINT8 *p)
{
    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 bmp_read_u32(const UINT8 *p)
{
    return (UINT32)p[0] |
           ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static BOOLEAN show_splash_for_three_seconds(
    const VOID *Data,
    UINTN Size
)
{
    const UINT8 *Bytes = (const UINT8 *)Data;
    UINT32 PixelOffset;
    UINT32 DibSize;
    INT32 Width;
    INT32 Height;
    UINT16 Planes;
    UINT16 BitsPerPixel;
    UINT32 Compression;
    UINTN BytesPerPixel;
    UINTN RowSize;
    UINTN y;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixels = NULL;
    EFI_STATUS Status;
    UINTN DestX;
    UINTN DestY;

    if (Data == NULL || Size < 54 ||
        Bytes[0] != 'B' || Bytes[1] != 'M')
        return FALSE;

    PixelOffset = bmp_read_u32(Bytes + 10);
    DibSize = bmp_read_u32(Bytes + 14);
    Width = (INT32)bmp_read_u32(Bytes + 18);
    Height = (INT32)bmp_read_u32(Bytes + 22);
    Planes = bmp_read_u16(Bytes + 26);
    BitsPerPixel = bmp_read_u16(Bytes + 28);
    Compression = bmp_read_u32(Bytes + 30);

    if (DibSize < 40 || (UINTN)14 + DibSize > Size ||
        Width != 128 || (Height != 128 && Height != -128) ||
        Planes != 1 || (BitsPerPixel != 24 && BitsPerPixel != 32) ||
        Compression != 0)
        return FALSE;

    BytesPerPixel = BitsPerPixel / 8;
    RowSize = ((128 * BytesPerPixel + 3) / 4) * 4;

    if ((UINTN)PixelOffset > Size ||
        RowSize * 128 > Size - (UINTN)PixelOffset)
        return FALSE;

    Status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &GraphicsOutputProtocol, NULL,
                               (VOID **)&Gop);
    if (EFI_ERROR(Status) || Gop == NULL || Gop->Mode == NULL ||
        Gop->Mode->Info == NULL)
        return FALSE;

    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               128 * 128 * sizeof(*Pixels),
                               (VOID **)&Pixels);
    if (EFI_ERROR(Status))
        return FALSE;

    for (y = 0; y < 128; ++y) {
        UINTN SourceY = Height > 0 ? 127 - y : y;
        const UINT8 *Row = Bytes + PixelOffset + SourceY * RowSize;
        UINTN x;

        for (x = 0; x < 128; ++x) {
            const UINT8 *Source = Row + x * BytesPerPixel;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixel =
                &Pixels[y * 128 + x];

            Pixel->Blue = Source[0];
            Pixel->Green = Source[1];
            Pixel->Red = Source[2];
            Pixel->Reserved = 0;
        }
    }

    DestX = Gop->Mode->Info->HorizontalResolution > 128
        ? (Gop->Mode->Info->HorizontalResolution - 128) / 2 : 0;
    DestY = Gop->Mode->Info->VerticalResolution > 128
        ? (Gop->Mode->Info->VerticalResolution - 128) / 2 : 0;

    Status = uefi_call_wrapper(Gop->Blt, 10,
                               Gop, Pixels, EfiBltBufferToVideo,
                               0, 0, DestX, DestY, 128, 128, 0);

    uefi_call_wrapper(BS->FreePool, 1, Pixels);

    if (EFI_ERROR(Status)) {
        Print(L"[SPLASH] GOP drawing failed: %r\r\n", Status);
        return FALSE;
    }

    Print(L"[SPLASH] Displaying splash.bmp for 3 seconds.\r\n");
    uefi_call_wrapper(BS->Stall, 1, 3000000);
    return TRUE;
}

#define DRIVER_DIRECTORY L"\\cookieos\\System\\Drivers"
#define DRIVER_INFO_BUFFER 2048

static BOOLEAN has_sys_extension(const CHAR16 *Name)
{
    UINTN Length = StrLen(Name);
    if (Length < 4)
        return FALSE;
    Name += Length - 4;
    return (Name[0] == L'.' &&
            (Name[1] == L's' || Name[1] == L'S') &&
            (Name[2] == L'y' || Name[2] == L'Y') &&
            (Name[3] == L's' || Name[3] == L'S'));
}

static EFI_STATUS load_driver_images(
    EFI_FILE_HANDLE Root,
    COOKIEOS_BOOT_INFO *Info
)
{
    EFI_STATUS Status;
    EFI_FILE_HANDLE Directory = NULL;
    EFI_FILE_INFO *Entry = NULL;
    UINTN EntrySize = DRIVER_INFO_BUFFER;

    Status = uefi_call_wrapper(Root->Open, 5, Root, &Directory,
                               DRIVER_DIRECTORY, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        Print(L"[DRIVER] No driver directory; continuing without drivers.\r\n");
        return EFI_SUCCESS;
    }

    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               EntrySize, (VOID **)&Entry);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Directory->Close, 1, Directory);
        return Status;
    }

    for (;;) {
        UINTN ReadSize = EntrySize;
        EFI_FILE_HANDLE File = NULL;
        VOID *Data = NULL;
        UINTN DataSize = 0;

        SetMem(Entry, EntrySize, 0);
        Status = uefi_call_wrapper(Directory->Read, 3,
                                   Directory, &ReadSize, Entry);
        if (EFI_ERROR(Status) || ReadSize == 0)
            break;

        if ((Entry->Attribute & EFI_FILE_DIRECTORY) != 0 ||
            !has_sys_extension(Entry->FileName))
            continue;

        if (Info->driver_count >= COOKIEOS_BOOT_MAX_DRIVERS) {
            Print(L"[DRIVER] Driver limit reached; ignoring remaining files.\r\n");
            break;
        }

        Status = uefi_call_wrapper(Directory->Open, 5,
                                   Directory, &File, Entry->FileName,
                                   EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(Status))
            continue;

        {
            EFI_FILE_INFO *FileInfo = NULL;
            EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
            UINTN FileInfoSize = SIZE_OF_EFI_FILE_INFO + 256;
            UINTN ActualSize;

            Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                                       FileInfoSize, (VOID **)&FileInfo);
            if (!EFI_ERROR(Status)) {
                Status = uefi_call_wrapper(File->GetInfo, 4, File,
                                           &FileInfoGuid, &FileInfoSize,
                                           FileInfo);
            }
            if (EFI_ERROR(Status) || FileInfo == NULL ||
                FileInfo->FileSize == 0 ||
                FileInfo->FileSize > 0xFFFFFFFFULL) {
                if (FileInfo != NULL)
                    uefi_call_wrapper(BS->FreePool, 1, FileInfo);
                uefi_call_wrapper(File->Close, 1, File);
                continue;
            }

            DataSize = (UINTN)FileInfo->FileSize;
            uefi_call_wrapper(BS->FreePool, 1, FileInfo);
            Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                                       DataSize, &Data);
            if (EFI_ERROR(Status)) {
                uefi_call_wrapper(File->Close, 1, File);
                continue;
            }

            ActualSize = DataSize;
            Status = uefi_call_wrapper(File->Read, 3, File,
                                       &ActualSize, Data);
            uefi_call_wrapper(File->Close, 1, File);
            if (EFI_ERROR(Status) || ActualSize != DataSize) {
                uefi_call_wrapper(BS->FreePool, 1, Data);
                continue;
            }
        }

        Info->drivers[Info->driver_count].image = (UINT64)(UINTN)Data;
        Info->drivers[Info->driver_count].image_size = DataSize;
        ++Info->driver_count;
        Print(L"[DRIVER] Preloaded %s (%u bytes).\r\n",
              Entry->FileName, DataSize);
    }

    uefi_call_wrapper(BS->FreePool, 1, Entry);
    uefi_call_wrapper(Directory->Close, 1, Directory);
    return EFI_SUCCESS;
}

static EFI_STATUS exit_boot_services_and_jump(
    EFI_HANDLE ImageHandle,
    COOKIEOS_BOOT_INFO *Info,
    UINT64 Entry,
    EFI_PHYSICAL_ADDRESS PageTableBase
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
     * This jump uses the linked virtual entry address; the kernel starts with
     * temporary page tables, which map kernel virtual addresses to its physical allocation.
     */
    /*
     * Replace the firmware page tables with our temporary identity map.
     * RDI remains the boot-info argument and RAX remains the kernel entry.
     */
    __asm__ __volatile__(
        "movq %%rcx, %%cr3\n\t"
        "jmp *%%rax"
        :
        : "D"(Info), "a"(Entry), "c"(PageTableBase)
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
    UINT64 KernelEntry = 0;
    COOKIEOS_BOOT_INFO *Info = NULL;
    EFI_PHYSICAL_ADDRESS PageTableBase = 0;

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
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }

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
                               &KernelBase, &KernelEnd, &KernelEntry);
    if (EFI_ERROR(Status)) {
        Print(L"[ELF] Segment loading failed: %r\r\n", Status);
        goto fail;
    }

    Print(L"[ELF] Loaded %u PT_LOAD segments.\r\n", SegmentCount);
    Print(L"[ELF] Kernel entry: %lx\r\n", KernelEntry);

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
    Info->kernel_entry = KernelEntry;
    collect_framebuffer(Info);

    /*
     * Preload the optional splash bitmap before ExitBootServices().
     * The kernel receives the file buffer through bootinfo ABI v3.
     */
    {
        VOID *Splash = NULL;
        UINTN SplashSize = 0;
        Status = read_entire_file(
            Root,
            L"\\cookieos\\System\\Resources\\splash.bmp",
            &Splash,
            &SplashSize
        );
        if (!EFI_ERROR(Status)) {
            Info->splash_image = (UINT64)(UINTN)Splash;
            Info->splash_image_size = SplashSize;
            Print(L"[SPLASH] Preloaded splash.bmp (%u bytes).\r\n",
                  SplashSize);
            show_splash_for_three_seconds(Splash, SplashSize);
        } else {
            Print(L"[SPLASH] splash.bmp unavailable; continuing without it.\r\n");
        }
    }

    Status = load_driver_images(Root, Info);
    uefi_call_wrapper(Root->Close, 1, Root);
    Root = NULL;
    if (EFI_ERROR(Status))
        goto fail;

    Status = create_identity_page_tables(Segments, SegmentCount, &PageTableBase);
    if (EFI_ERROR(Status)) {
        Print(L"[ELF] Could not create identity page tables: %r\r\n",
              Status);
        goto fail;
    }

    Print(L"[ELF] Exiting UEFI boot services...\r\n");

    Status = exit_boot_services_and_jump(
        ImageHandle, Info, KernelEntry, PageTableBase
    );

fail:
    if (Root != NULL)
        uefi_call_wrapper(Root->Close, 1, Root);
    if (Image != NULL)
        uefi_call_wrapper(BS->FreePool, 1, Image);

    return Status;
}
