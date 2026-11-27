/*
 * CookieOS Server 2026 - MT kernel
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "../../boot/include/bootinfo.h"
#include "framebuffer.h"
#include "driver_loader.h"
#include "usermode.h"
#include "fat.h"
#include <stdint.h>

static uint8_t autoexec_image[2U * 1024U * 1024U] __attribute__((aligned(16)));

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
    __asm__ __volatile__("outb %0, $0xE9" : : "a"((unsigned char)0x50));
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
    /* First C-level instruction marker for QEMU debugcon (I/O port 0xE9). */
    __asm__ __volatile__(
        "movw $0xE9, %%dx\n"
        "movb $'C', %%al\n"
        "outb %%al, %%dx\n"
        :
        :
        : "rax", "rdx"
    );

    if (!info)
        kernel_panic("CookieBoot supplied a NULL boot-information pointer");
    __asm__ __volatile__("outb %0, $0xE9" : : "a"((unsigned char)0x4E));

    if (info->magic != COOKIEOS_BOOTINFO_MAGIC)
        kernel_panic("Invalid boot-information magic");
    __asm__ __volatile__("outb %0, $0xE9" : : "a"((unsigned char)0x47));

    /*
     * ABI v1 predates the preloaded-driver array. Accept it so an older
     * CookieBoot on the ESP can still boot this kernel safely.
     */
    if (info->version < 1U ||
        info->version > COOKIEOS_BOOTINFO_VERSION)
        kernel_panic("Unsupported boot-information ABI version");
    __asm__ __volatile__("outb %0, $0xE9" : : "a"((unsigned char)0x56));

    if (info->version == 1U) {
        if (info->size < __builtin_offsetof(COOKIEOS_BOOT_INFO, driver_count))
            kernel_panic("ABI v1 boot-information structure is too small");
    } else if (info->size < sizeof(COOKIEOS_BOOT_INFO)) {
        kernel_panic("ABI v2 boot-information structure is too small");
    }
    __asm__ __volatile__("outb %0, $0xE9" : : "a"((unsigned char)0x5A));

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
    if (info->version >= 2U)
        driver_load_all(info);
    else
        framebuffer_write("[DRIVER] Skipping preload: CookieBoot uses ABI v1.\\n");

    framebuffer_write("[MT] Early initialization complete.\n");
    framebuffer_write("[STORAGE] Initializing PCI AHCI SATA controller...\n");
    if (!fat_init())
        kernel_panic("Could not initialize ATA disk or FAT16/32 volume");

    framebuffer_write("[FAT] Reading \\cookieos\\System\\autoexec.app...\n");
    {
        size_t app_size = 0;
        if (!fat_read_file("cookieos/system/autoexec.app",
                           autoexec_image, sizeof(autoexec_image),
                           &app_size))
            kernel_panic("Could not read \\cookieos\\System\\autoexec.app");

        framebuffer_write("[FAT] Loaded autoexec.app (");
        framebuffer_write_dec(app_size);
        framebuffer_write(" bytes)\n");
        usermode_run_app(autoexec_image, app_size);
    }

    kernel_panic("External application unexpectedly returned");
}