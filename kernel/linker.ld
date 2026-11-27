/*
 * CookieOS Server 2026 - x86-64 kernel linker script
 * SPDX-License-Identifier: GPL-2.0-only
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x00100000;
    __kernel_start = .;

    .text ALIGN(0x1000) :
    {
        KEEP(*(.text))
        *(.text.*)
    } :text

    .rodata ALIGN(0x1000) :
    {
        *(.rodata)
        *(.rodata.*)
    } :text

    .data ALIGN(0x1000) :
    {
        *(.data)
        *(.data.*)
    } :data

    .bss ALIGN(0x1000) (NOLOAD) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    } :data

    . = ALIGN(0x1000);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}