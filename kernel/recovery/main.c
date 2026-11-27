/*
 * CookieOS Server 2026 - CookieOS Recovery Environment (COSRE)
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "../../boot/include/bootinfo.h"
#include "../include/framebuffer.h"
#include <stdint.h>

#define RECOVERY_BOOTINFO_MIN_SIZE \
    __builtin_offsetof(COOKIEOS_BOOT_INFO, driver_count)

static __attribute__((noreturn)) void recovery_halt(void)
{
    __asm__ __volatile__(
        "cli\n"
        "1: hlt\n"
        "jmp 1b\n"
    );
    __builtin_unreachable();
}

static void recovery_print_boot_info(const COOKIEOS_BOOT_INFO *info)
{
    framebuffer_write("[COSRE] Boot information\n");
    framebuffer_write("  ABI version: ");
    framebuffer_write_dec(info->version);
    framebuffer_write("\n  Structure size: ");
    framebuffer_write_dec(info->size);
    framebuffer_write(" bytes\n  ACPI RSDP: ");
    framebuffer_write_hex(info->acpi_rsdp);
    framebuffer_write("\n  Memory map: ");
    framebuffer_write_hex(info->memory_map);
    framebuffer_write("\n  Memory map size: ");
    framebuffer_write_dec(info->memory_map_size);
    framebuffer_write(" bytes\n");

    if (info->framebuffer_base) {
        framebuffer_write("  GOP framebuffer: ");
        framebuffer_write_hex(info->framebuffer_base);
        framebuffer_write("\n  Resolution: ");
        framebuffer_write_dec(info->framebuffer_width);
        framebuffer_write(" x ");
        framebuffer_write_dec(info->framebuffer_height);
        framebuffer_write("\n");
    } else {
        framebuffer_write("  GOP framebuffer: unavailable\n");
    }
}

__attribute__((noreturn))
void recovery_main(const COOKIEOS_BOOT_INFO *info)
{
    if (!info || info->magic != COOKIEOS_BOOTINFO_MAGIC)
        recovery_halt();

    if (info->version < 1U ||
        info->version > COOKIEOS_BOOTINFO_VERSION ||
        info->size < RECOVERY_BOOTINFO_MIN_SIZE)
        recovery_halt();

    framebuffer_init(info);
    if (!framebuffer_available())
        recovery_halt();

    framebuffer_clear();

    if (info->version >= 3U &&
        info->size >= sizeof(COOKIEOS_BOOT_INFO) &&
        info->splash_image && info->splash_image_size) {
        (void)framebuffer_draw_bmp(
            (const void *)(uintptr_t)info->splash_image,
            (size_t)info->splash_image_size
        );
    }

    framebuffer_write("\n========================================\n");
    framebuffer_write(" COOKIEOS RECOVERY ENVIRONMENT\n");
    framebuffer_write(" COSRE - CookieOS Server 2026\n");
    framebuffer_write("========================================\n");
    framebuffer_write("[COSRE] Recovery kernel started.\n");
    framebuffer_write("[COSRE] Normal kernel was not selected or could not load.\n\n");

    recovery_print_boot_info(info);

    framebuffer_write("\n[COSRE] Recovery environment is running.\n");
    framebuffer_write("[COSRE] This build provides boot diagnostics.\n");
    framebuffer_write("[COSRE] System halted; reset to return to CookieBoot.\n");

    recovery_halt();
}
