/* CookieOS Server 2026 - external autoexec application
 * SPDX-License-Identifier: GPL-2.0-only
 */
.section .text
.global _start
.type _start,@function
_start:
    leaq message(%rip), %rdi
    movq $message_end-message, %rsi
    movq $1, %rax
    int $0x80
    xorq %rdi, %rdi
    xorq %rax, %rax
    int $0x80
1:  hlt
    jmp 1b
.section .rodata
message:
    .ascii "[APP] External autoexec.app is running in Ring 3!\n"
message_end:
.section .note.GNU-stack,"",@progbits
