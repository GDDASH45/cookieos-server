/*
 * CookieOS Server 2026 - COSRE x86-64 entry
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
    movb $'R', %al
    outb %al, %dx

    leaq recovery_stack_top(%rip), %rsp
    andq $-16, %rsp

    call recovery_main

.Lrecovery_halt:
    cli
    hlt
    jmp .Lrecovery_halt

.size _start, . - _start

.section .bss
.balign 16
recovery_stack:
    .skip 16384
recovery_stack_top:

.section .note.GNU-stack,"",@progbits
