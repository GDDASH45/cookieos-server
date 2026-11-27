/*
 * CookieOS Server 2026 - x86-64 kernel entry
 * SPDX-License-Identifier: GPL-2.0-only
 */

.section .text
.global _start
.type _start, @function

_start:
    cli
    cld

    /* CookieBoot passes COOKIEOS_BOOT_INFO in RDI. */
    movw $0xE9, %dx
    movb $'E', %al
    outb %al, %dx

    /*
     * Keep the active UEFI stack for the initial C entry.
     *
     * CookieBoot has just transferred control with the firmware page tables
     * still active. The kernel's .bss stack is not guaranteed to be mapped
     * by those page tables yet. The current bootloader stack is already
     * mapped and remains allocated after ExitBootServices().
     *
     * Align for the System V AMD64 ABI: RSP must be 16-byte aligned before
     * CALL, so kernel_main observes RSP % 16 == 8 on entry.
     */
    andq $-16, %rsp

    movw $0xE9, %dx
    movb $'S', %al
    outb %al, %dx

    /* Enter the C kernel using the x86-64 System V ABI. */
    call kernel_main

.Lhalt:
    cli
    hlt
    jmp .Lhalt

.size _start, . - _start

.section .note.GNU-stack,"",@progbits
