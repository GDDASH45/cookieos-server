/*
 * CookieOS Server 2026 - x86-64 privilege transition and syscall entry
 * SPDX-License-Identifier: GPL-2.0-only
 */
.section .text
.code64
.global arch_load_gdt
.type arch_load_gdt, @function
arch_load_gdt:
    lgdt (%rdi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    ret
.global arch_load_idt
.type arch_load_idt, @function
arch_load_idt:
    lidt (%rdi)
    ret
.global arch_load_tr
.type arch_load_tr, @function
arch_load_tr:
    movw $0x28, %ax
    ltr %ax
    ret
.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
    cli
    movw $0x1b, %ax
    movw %ax, %ds
    movw %ax, %es
    pushq $0x1b
    pushq %rsi
    pushq $0x02
    pushq $0x23
    pushq %rdi
    iretq
.global ring3_syscall_entry
.type ring3_syscall_entry, @function
ring3_syscall_entry:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    call syscall_dispatch
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq
.global ring3_fatal_exception
.type ring3_fatal_exception, @function
ring3_fatal_exception:
    cli
    movw $0xE9, %dx
    movb $'X', %al
    outb %al, %dx
2:
    hlt
    jmp 2b
.section .note.GNU-stack,"",@progbits
