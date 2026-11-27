/*
 * CookieOS Server 2026 - internal ELF64 relocatable driver loader
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stddef.h>
#include <stdint.h>
#include <bootinfo.h>
#include <driver.h>
#include "driver_loader.h"
#include "framebuffer.h"

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_REL 1
#define EM_X86_64 62
#define SHT_SYMTAB 2
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHF_ALLOC 2
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define DRIVER_POOL_SIZE (4U * 1024U * 1024U)
#define MAX_SECTIONS 512U

typedef struct {
    unsigned char ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} ELF64_EHDR;

typedef struct {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
} ELF64_SHDR;

typedef struct {
    uint32_t name;
    unsigned char info;
    unsigned char other;
    uint16_t shndx;
    uint64_t value;
    uint64_t size;
} ELF64_SYM;

typedef struct {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} ELF64_RELA;

typedef struct {
    unsigned int magic;
    unsigned int abi_version;
    const char *name;
    const char *version;
} DRIVER_INFO;

typedef struct {
    const char *name;
    void (*exit)(void);
} LOADED_DRIVER;

static unsigned char driver_pool[DRIVER_POOL_SIZE]
    __attribute__((aligned(4096)));
static size_t driver_pool_used;
static LOADED_DRIVER loaded_drivers[COOKIEOS_BOOT_MAX_DRIVERS];
static unsigned int loaded_driver_count;

static int range_ok(uint64_t offset, uint64_t length, uint64_t total)
{
    return offset <= total && length <= total - offset;
}

static int string_ok(const char *table, size_t size, uint32_t offset)
{
    size_t i;
    if (offset >= size)
        return 0;
    for (i = offset; i < size; ++i)
        if (table[i] == '\0')
            return 1;
    return 0;
}

static int name_equals(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        ++i;
    return a[i] == b[i];
}

static void *pool_alloc(size_t size, size_t alignment)
{
    size_t start;
    if (alignment == 0)
        alignment = 1;
    if ((alignment & (alignment - 1)) != 0)
        return NULL;
    if (driver_pool_used > SIZE_MAX - (alignment - 1))
        return NULL;
    start = (driver_pool_used + alignment - 1) & ~(alignment - 1);
    if (start > DRIVER_POOL_SIZE || size > DRIVER_POOL_SIZE - start)
        return NULL;
    driver_pool_used = start + size;
    return &driver_pool[start];
}

static int resolve_symbol(
    const ELF64_SYM *symbol,
    const uintptr_t *section_addresses,
    uint16_t section_count,
    uintptr_t *result
)
{
    if (symbol->shndx == SHN_UNDEF)
        return symbol->name == 0 && symbol->value == 0
            ? (*result = 0, 1) : 0;
    if (symbol->shndx == SHN_ABS) {
        *result = (uintptr_t)symbol->value;
        return 1;
    }
    if (symbol->shndx >= section_count ||
        section_addresses[symbol->shndx] == 0)
        return 0;
    *result = section_addresses[symbol->shndx] +
              (uintptr_t)symbol->value;
    return 1;
}

static int relocate_module(
    const unsigned char *image,
    size_t image_size,
    const ELF64_EHDR *eh,
    const ELF64_SHDR *sections,
    const uintptr_t *section_addresses
)
{
    unsigned int i;

    for (i = 0; i < eh->shnum; ++i) {
        const ELF64_SHDR *relsec = &sections[i];
        const ELF64_SHDR *symsec;
        const ELF64_SYM *symbols;
        const char *strings;
        size_t symbol_count;
        size_t j;

        if (relsec->type != SHT_RELA)
            continue;
        if (relsec->info >= eh->shnum ||
            relsec->link >= eh->shnum ||
            relsec->entsize != sizeof(ELF64_RELA) ||
            relsec->size % sizeof(ELF64_RELA) != 0 ||
            !range_ok(relsec->offset, relsec->size, image_size))
            return 0;

        symsec = &sections[relsec->link];
        if (symsec->type != SHT_SYMTAB ||
            symsec->entsize != sizeof(ELF64_SYM) ||
            symsec->size % sizeof(ELF64_SYM) != 0 ||
            symsec->link >= eh->shnum ||
            !range_ok(symsec->offset, symsec->size, image_size))
            return 0;

        {
            const ELF64_SHDR *strsec = &sections[symsec->link];
            if (strsec->type != 3 ||
                !range_ok(strsec->offset, strsec->size, image_size))
                return 0;
            strings = (const char *)(image + strsec->offset);
        }

        symbols = (const ELF64_SYM *)(image + symsec->offset);
        symbol_count = (size_t)(symsec->size / sizeof(ELF64_SYM));

        for (j = 0; j < relsec->size / sizeof(ELF64_RELA); ++j) {
            const ELF64_RELA *rela =
                (const ELF64_RELA *)(image + relsec->offset) + j;
            uint32_t symbol_index = (uint32_t)(rela->info >> 32);
            uint32_t type = (uint32_t)rela->info;
            uintptr_t symbol_address;
            uintptr_t place;
            int64_t value;
            unsigned char *target;

            if (symbol_index >= symbol_count ||
                section_addresses[relsec->info] == 0)
                return 0;
            if (rela->offset > sections[relsec->info].size ||
                sizeof(uint32_t) >
                    sections[relsec->info].size - rela->offset)
                return 0;
            if (!resolve_symbol(&symbols[symbol_index],
                                section_addresses, eh->shnum,
                                &symbol_address))
                return 0;

            if (symbols[symbol_index].shndx == SHN_UNDEF &&
                symbols[symbol_index].name != 0) {
                if (!string_ok(strings, (size_t)sections[symsec->link].size,
                               symbols[symbol_index].name))
                    return 0;
                framebuffer_write("[DRIVER] Unresolved import: ");
                framebuffer_write(strings + symbols[symbol_index].name);
                framebuffer_write("\n");
                return 0;
            }

            target = (unsigned char *)section_addresses[relsec->info] +
                     rela->offset;
            place = (uintptr_t)target;
            value = (int64_t)symbol_address + rela->addend;

            switch (type) {
            case 0: /* R_X86_64_NONE */
                break;
            case R_X86_64_64:
                if (rela->offset > sections[relsec->info].size ||
                    sizeof(uint64_t) >
                        sections[relsec->info].size - rela->offset)
                    return 0;
                *(uint64_t *)target = (uint64_t)value;
                break;
            case R_X86_64_PC32:
            case R_X86_64_PLT32:
                value -= (int64_t)place;
                if (value < INT32_MIN || value > INT32_MAX)
                    return 0;
                *(int32_t *)target = (int32_t)value;
                break;
            case R_X86_64_32:
                if (value < 0 || (uint64_t)value > UINT32_MAX)
                    return 0;
                *(uint32_t *)target = (uint32_t)value;
                break;
            case R_X86_64_32S:
                if (value < INT32_MIN || value > INT32_MAX)
                    return 0;
                *(int32_t *)target = (int32_t)value;
                break;
            default:
                framebuffer_write("[DRIVER] Unsupported ELF relocation\n");
                return 0;
            }
        }
    }
    return 1;
}

static int load_one_driver(const COOKIEOS_BOOT_DRIVER *boot_driver)
{
    const unsigned char *image =
        (const unsigned char *)(uintptr_t)boot_driver->image;
    size_t image_size = (size_t)boot_driver->image_size;
    const ELF64_EHDR *eh;
    const ELF64_SHDR *sections;
    uintptr_t section_addresses[MAX_SECTIONS] = {0};
    const ELF64_SHDR *symsec = NULL;
    const ELF64_SHDR *strsec = NULL;
    const ELF64_SYM *symbols;
    const char *strings;
    size_t symbol_count;
    unsigned int i;
    int (*init)(void) = NULL;
    void (*exit_fn)(void) = NULL;
    const DRIVER_INFO *metadata = NULL;

    if (!image || image_size < sizeof(ELF64_EHDR))
        return 0;
    eh = (const ELF64_EHDR *)image;

    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F' ||
        eh->ident[4] != ELFCLASS64 || eh->ident[5] != ELFDATA2LSB ||
        eh->type != ET_REL || eh->machine != EM_X86_64 ||
        eh->shentsize != sizeof(ELF64_SHDR) ||
        eh->shnum == 0 || eh->shnum > MAX_SECTIONS ||
        !range_ok(eh->shoff,
                  (uint64_t)eh->shnum * sizeof(ELF64_SHDR), image_size))
        return 0;

    sections = (const ELF64_SHDR *)(image + eh->shoff);

    for (i = 0; i < eh->shnum; ++i) {
        const ELF64_SHDR *section = &sections[i];
        size_t alignment;
        void *memory;

        if (section->type == SHT_SYMTAB)
            symsec = section;
        if (section->type != SHT_NOBITS &&
            !range_ok(section->offset, section->size, image_size))
            return 0;
        if (!(section->flags & SHF_ALLOC) || section->size == 0)
            continue;

        alignment = section->addralign ? (size_t)section->addralign : 1;
        memory = pool_alloc((size_t)section->size, alignment);
        if (!memory)
            return 0;
        section_addresses[i] = (uintptr_t)memory;

        if (section->type == SHT_NOBITS) {
            unsigned char *p = memory;
            size_t n;
            for (n = 0; n < section->size; ++n)
                p[n] = 0;
        } else {
            const unsigned char *src = image + section->offset;
            unsigned char *dst = memory;
            size_t n;
            for (n = 0; n < section->size; ++n)
                dst[n] = src[n];
        }
    }

    if (!symsec || symsec->link >= eh->shnum ||
        symsec->entsize != sizeof(ELF64_SYM) ||
        symsec->size % sizeof(ELF64_SYM) != 0)
        return 0;
    strsec = &sections[symsec->link];
    if (strsec->type != 3 ||
        !range_ok(strsec->offset, strsec->size, image_size))
        return 0;

    symbols = (const ELF64_SYM *)(image + symsec->offset);
    symbol_count = (size_t)(symsec->size / sizeof(ELF64_SYM));
    strings = (const char *)(image + strsec->offset);

    if (!relocate_module(image, image_size, eh, sections,
                         section_addresses))
        return 0;

    for (i = 0; i < symbol_count; ++i) {
        uintptr_t address;
        if (!string_ok(strings, (size_t)strsec->size, symbols[i].name))
            continue;
        if (!resolve_symbol(&symbols[i], section_addresses,
                            eh->shnum, &address))
            continue;
        if (name_equals(strings + symbols[i].name, "driver_init"))
            init = (int (*)(void))address;
        else if (name_equals(strings + symbols[i].name, "driver_exit"))
            exit_fn = (void (*)(void))address;
        else if (name_equals(strings + symbols[i].name,
                             "cookieos_driver_info"))
            metadata = (const DRIVER_INFO *)address;
    }

    if (!init || !metadata ||
        metadata->magic != COOKIEOS_DRIVER_MAGIC ||
        metadata->abi_version != COOKIEOS_DRIVER_ABI_VERSION ||
        !metadata->name || !metadata->version)
        return 0;

    framebuffer_write("[DRIVER] Initializing ");
    framebuffer_write(metadata->name);
    framebuffer_write(" ");
    framebuffer_write(metadata->version);
    framebuffer_write("... ");

    if (init() != 0) {
        framebuffer_write("FAILED\n");
        return 0;
    }

    framebuffer_write("OK\n");
    if (loaded_driver_count < COOKIEOS_BOOT_MAX_DRIVERS) {
        loaded_drivers[loaded_driver_count].name = metadata->name;
        loaded_drivers[loaded_driver_count].exit = exit_fn;
        ++loaded_driver_count;
    }
    return 1;
}

void driver_load_all(const COOKIEOS_BOOT_INFO *boot_info)
{
    unsigned int i;
    if (!boot_info)
        return;

    framebuffer_write("[DRIVER] Found ");
    framebuffer_write_dec(boot_info->driver_count);
    framebuffer_write(" driver image(s) on the ESP.\n");

    for (i = 0; i < boot_info->driver_count &&
                i < COOKIEOS_BOOT_MAX_DRIVERS; ++i) {
        if (!load_one_driver(&boot_info->drivers[i]))
            framebuffer_write("[DRIVER] Driver rejected or failed to load.\n");
    }

    framebuffer_write("[DRIVER] Loaded ");
    framebuffer_write_dec(loaded_driver_count);
    framebuffer_write(" driver(s).\n");
}
