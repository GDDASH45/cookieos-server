/*
 * CookieOS Server 2026 - x86-64 kernel entry
 * SPDX-License-Identifier: GPL-2.0-only
 */

.section .text
.global _start
.global kernel_stack_top
.type _start, @function

_start:
    cli
    cld

    /* CookieBoot passes COOKIEOS_BOOT_INFO in RDI. */
    movw $0xE9, %dx
    movb $'E', %al
    outb %al, %dx

    /*
     * Switch to the kernel-owned stack.
     *
     * The stack is reserved in .bss. CookieBoot allocates every PT_LOAD
     * segment using p_memsz and zeroes the complete allocation, including
     * the .bss portion, before transferring control to this entry point.
     * The kernel uses identity virtual/physical addresses at this stage,
     * so the stack is accessible under the firmware's active page tables.
     */
    leaq kernel_stack_top(%rip), %rsp
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

.section .bss
.balign 16
kernel_stack:
    .skip 16384
kernel_stack_top:

.section .note.GNU-stack,"",@progbits