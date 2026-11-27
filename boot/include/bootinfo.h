/*
 * CookieOS Server 2026 boot information ABI
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_BOOTINFO_H
#define COOKIEOS_BOOTINFO_H

#define COOKIEOS_BOOTINFO_MAGIC 0x434F4F4B49454249ULL
#define COOKIEOS_BOOTINFO_VERSION 1

typedef struct {
    unsigned long long magic;
    unsigned int version;
    unsigned int size;

    unsigned long long memory_map;
    unsigned long long memory_map_size;
    unsigned long long memory_descriptor_size;
    unsigned int memory_descriptor_version;
    unsigned int reserved0;

    unsigned long long acpi_rsdp;

    unsigned long long framebuffer_base;
    unsigned long long framebuffer_size;
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    unsigned int framebuffer_pixels_per_scanline;
    unsigned int framebuffer_pixel_format;

    unsigned long long kernel_physical_base;
    unsigned long long kernel_size;
    unsigned long long kernel_entry;
} COOKIEOS_BOOT_INFO;

#endif
