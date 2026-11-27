/*
 * CookieOS Server 2026 - MT kernel
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <bootinfo.h>
#include "serial.h"

#define COOKIEOS_KERNEL_NAME "CookieOS Server"
#define COOKIEOS_KERNEL_VERSION "2026"

static __attribute__((noreturn)) void kernel_halt(void)
{
    __asm__ __volatile__(
        "cli\n"
        "1: hlt\n"
        "jmp 1b\n"
    );

    __builtin_unreachable();
}

static __attribute__((noreturn)) void kernel_panic(const char *message)
{
    serial_write("\n[KERNEL PANIC] ");
    serial_write(message);
    serial_write("\n");
    kernel_halt();
}

static void print_boot_info(const COOKIEOS_BOOT_INFO *info)
{
    serial_write("[BOOT] ABI version: ");
    serial_write_dec(info->version);
    serial_write("\n[BOOT] Boot info size: ");
    serial_write_dec(info->size);
    serial_write(" bytes\n");

    serial_write("[BOOT] Kernel physical base: ");
    serial_write_hex(info->kernel_physical_base);
    serial_write("\n[BOOT] Kernel image span: ");
    serial_write_hex(info->kernel_size);
    serial_write("\n[BOOT] Kernel entry: ");
    serial_write_hex(info->kernel_entry);
    serial_write("\n");

    serial_write("[BOOT] UEFI memory map: ");
    serial_write_hex(info->memory_map);
    serial_write(" (");
    serial_write_dec(info->memory_map_size);
    serial_write(" bytes, descriptor size ");
    serial_write_dec(info->memory_descriptor_size);
    serial_write(")\n");

    serial_write("[BOOT] ACPI RSDP: ");
    serial_write_hex(info->acpi_rsdp);
    serial_write("\n");

    if (info->framebuffer_base) {
        serial_write("[BOOT] GOP framebuffer: ");
        serial_write_hex(info->framebuffer_base);
        serial_write(", ");
        serial_write_dec(info->framebuffer_width);
        serial_write("x");
        serial_write_dec(info->framebuffer_height);
        serial_write(", pitch ");
        serial_write_dec(info->framebuffer_pixels_per_scanline);
        serial_write("\n");
    } else {
        serial_write("[BOOT] GOP framebuffer unavailable\n");
    }
}

__attribute__((noreturn))
void kernel_main(const COOKIEOS_BOOT_INFO *info)
{
    serial_init();

    serial_write("\n");
    serial_write("========================================\n");
    serial_write(" CookieOS Server 2026\n");
    serial_write(" Modular Technology kernel\n");
    serial_write("========================================\n");

    if (info == 0)
        kernel_panic("CookieBoot supplied a NULL boot-information pointer");

    if (info->magic != COOKIEOS_BOOTINFO_MAGIC)
        kernel_panic("Invalid boot-information magic");

    if (info->version != COOKIEOS_BOOTINFO_VERSION)
        kernel_panic("Unsupported boot-information ABI version");

    if (info->size < sizeof(COOKIEOS_BOOT_INFO))
        kernel_panic("Boot-information structure is truncated");

    serial_write("[BOOT] CookieBoot handoff validated\n");
    print_boot_info(info);

    serial_write("\n[MT] Kernel entry reached successfully.\n");
    serial_write("[MT] Early initialization complete.\n");
    serial_write("[MT] CPU is halted until the next kernel subsystem is added.\n");

    kernel_halt();
}