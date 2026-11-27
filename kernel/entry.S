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
    leaq kernel_stack_top(%rip), %rsp
    andq $-16, %rsp

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