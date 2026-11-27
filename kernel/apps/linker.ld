/* CookieOS Server 2026 userspace ELF64 linker script
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
    . = 0x40000000;
    .text : ALIGN(0x1000)
    {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.*)
    } :text

    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    } :data

    .bss : ALIGN(0x1000)
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
