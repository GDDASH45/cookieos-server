/* CookieOS Server 2026 userspace ELF64 linker script
 * SPDX-License-Identifier: GPL-2.0-only
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS { app PT_LOAD FLAGS(5); }
SECTIONS
{
    . = 0x40000000;
    .text : ALIGN(0x1000) { *(.text .text.*) } :app
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) } :app
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
