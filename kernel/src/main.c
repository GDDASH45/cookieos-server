/*
 * CookieOS Server 2026 - MT kernel
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <bootinfo.h>
#include "framebuffer.h"
#include "driver_loader.h"

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
    framebuffer_write("\n[KERNEL PANIC] ");
    framebuffer_write(message);
    framebuffer_write("\n");
    kernel_halt();
}

static void print_boot_info(const COOKIEOS_BOOT_INFO *info)
{
    framebuffer_write("[BOOT] ABI version: ");
    framebuffer_write_dec(info->version);
    framebuffer_write("\n[BOOT] Boot info size: ");
    framebuffer_write_dec(info->size);
    framebuffer_write(" bytes\n");

    framebuffer_write("[BOOT] Kernel physical base: ");
    framebuffer_write_hex(info->kernel_physical_base);
    framebuffer_write("\n[BOOT] Kernel image span: ");
    framebuffer_write_hex(info->kernel_size);
    framebuffer_write("\n[BOOT] Kernel entry: ");
    framebuffer_write_hex(info->kernel_entry);
    framebuffer_write("\n");

    framebuffer_write("[BOOT] UEFI memory map: ");
    framebuffer_write_hex(info->memory_map);
    framebuffer_write(" (");
    framebuffer_write_dec(info->memory_map_size);
    framebuffer_write(" bytes, descriptor size ");
    framebuffer_write_dec(info->memory_descriptor_size);
    framebuffer_write(")\n");

    framebuffer_write("[BOOT] ACPI RSDP: ");
    framebuffer_write_hex(info->acpi_rsdp);
    framebuffer_write("\n");

    if (info->framebuffer_base) {
        framebuffer_write("[BOOT] GOP framebuffer: ");
        framebuffer_write_hex(info->framebuffer_base);
        framebuffer_write(", ");
        framebuffer_write_dec(info->framebuffer_width);
        framebuffer_write("x");
        framebuffer_write_dec(info->framebuffer_height);
        framebuffer_write(", pitch ");
        framebuffer_write_dec(info->framebuffer_pixels_per_scanline);
        framebuffer_write("\n");
    } else {
        framebuffer_write("[BOOT] GOP framebuffer unavailable\n");
    }
}

__attribute__((noreturn))
void kernel_main(const COOKIEOS_BOOT_INFO *info)
{
    __asm__ __volatile__(
        "movw $0xE9, %%dx\n"
        "movb     if (!info)
        kernel_panic("CookieBoot supplied a NULL boot-information pointer");

    if (info->magic != COOKIEOS_BOOTINFO_MAGIC)
        kernel_panic("Invalid boot-information magic");

    if (info->version != COOKIEOS_BOOTINFO_VERSION)
        kernel_panic("Unsupported boot-information ABI version");

    if (info->size < sizeof(COOKIEOS_BOOT_INFO))
        kernel_halt();

    framebuffer_init(info);
    if (!framebuffer_available())
        kernel_halt();

    framebuffer_clear();
    framebuffer_write("\n========================================\n");
    framebuffer_write(" CookieOS Server 2026\n");
    framebuffer_write(" Modular Technology kernel\n");
    framebuffer_write("========================================\n");
    framebuffer_write("[BOOT] CookieBoot handoff validated\n");
    print_boot_info(info);

    framebuffer_write("\n[MT] Kernel entry reached successfully.\n");
    framebuffer_write("[MT] GOP framebuffer console initialized.\n");
    driver_load_all(info);

    framebuffer_write("[MT] Early initialization complete.\n");
    framebuffer_write("[MT] CPU is halted until the next kernel subsystem is added.\n");

    kernel_halt();
}
M', %%al\n"
        "outb %%al, %%dx\n"
        :
        :
        : "rax", "rdx"
    );

    if (!info)
        kernel_panic("CookieBoot supplied a NULL boot-information pointer");

    if (info->magic != COOKIEOS_BOOTINFO_MAGIC)
        kernel_panic("Invalid boot-information magic");

    if (info->version != COOKIEOS_BOOTINFO_VERSION)
        kernel_panic("Unsupported boot-information ABI version");

    if (info->size < sizeof(COOKIEOS_BOOT_INFO))
        kernel_halt();

    __asm__ __volatile__(
        "movw $0xE9, %%dx\n"
        "movb $'M', %%al\n"
        "outb %%al, %%dx\n"
        :
        :
        : "rax", "rdx"
    );

    framebuffer_init(info);
    if (!framebuffer_available())
        kernel_halt();

    framebuffer_clear();
    framebuffer_write("\n========================================\n");
    framebuffer_write(" CookieOS Server 2026\n");
    framebuffer_write(" Modular Technology kernel\n");
    framebuffer_write("========================================\n");
    framebuffer_write("[BOOT] CookieBoot handoff validated\n");
    print_boot_info(info);

    framebuffer_write("\n[MT] Kernel entry reached successfully.\n");
    framebuffer_write("[MT] GOP framebuffer console initialized.\n");
    driver_load_all(info);

    framebuffer_write("[MT] Early initialization complete.\n");
    framebuffer_write("[MT] CPU is halted until the next kernel subsystem is added.\n");

    kernel_halt();
}
