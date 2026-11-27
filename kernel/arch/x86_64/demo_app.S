/*
 * CookieOS Server 2026 - built-in Ring 3 demonstration application
 * SPDX-License-Identifier: GPL-2.0-only
 */

.section .userapp,"ax",@progbits
.balign 4096
.global user_demo_entry
.global user_demo_page
user_demo_page:
user_demo_entry:
    movq $1, %rax
    leaq user_demo_message(%rip), %rdi
    movq $user_demo_message_end-user_demo_message, %rsi
    int $0x80

    movq $0, %rax
    xorq %rdi, %rdi
    int $0x80

1:
    jmp 1b

user_demo_message:
    .ascii "[APP] Hello from Ring 3!\n"
user_demo_message_end:

.section .note.GNU-stack,"",@progbits
