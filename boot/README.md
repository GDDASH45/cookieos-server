# CookieBoot ELF64 handoff

SPDX-License-Identifier: GPL-2.0-only

CookieBoot loads little-endian ELF64 ET_EXEC images for EM_X86_64. It loads each PT_LOAD segment at p_paddr, falling back to p_vaddr when p_paddr is zero. The image must be linked for those physical addresses. Dynamic relocations, ET_DYN, KASLR, and page-table construction are not implemented.

After loading, CookieBoot obtains the UEFI memory map, calls ExitBootServices(), and jumps to the translated physical entry address. At entry, RDI points to COOKIEOS_BOOT_INFO as defined in include/bootinfo.h. The kernel must establish its own execution environment and page tables; CookieBoot leaves the firmware page tables active.

## Boot information ABI v1

- magic: COOKIEOS_BOOTINFO_MAGIC
- version and size: ABI version and structure size
- memory_map, memory_map_size, memory_descriptor_size, memory_descriptor_version: UEFI memory map
- acpi_rsdp: ACPI RSDP physical address, or zero if unavailable
- framebuffer fields: GOP framebuffer information; zero if GOP is unavailable
- kernel_physical_base, kernel_size, kernel_entry: loaded kernel image range and physical entry

The memory map buffer and boot information structure are allocated as loader data and remain reserved after ExitBootServices().
