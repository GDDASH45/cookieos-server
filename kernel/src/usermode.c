/*
 * CookieOS Server 2026 - x86-64 Ring 3 setup and minimal syscall ABI
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "usermode.h"
#include "framebuffer.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT 0x001ULL
#define PAGE_WRITE   0x002ULL
#define PAGE_USER    0x004ULL
#define PAGE_HUGE    0x080ULL
#define PAGE_NX      (1ULL << 63)
#define USER_BASE    0x40000000ULL
#define USER_STACK   (USER_BASE + 0x2000ULL)
#define USER_CODE    USER_BASE
#define USER_DATA    (USER_BASE + 0x1000ULL)

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} DESCRIPTOR_POINTER;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} IDT_POINTER;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} TSS64;

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} IDT_GATE;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
} SYSCALL_FRAME;

extern void arch_load_gdt(const DESCRIPTOR_POINTER *pointer);
extern void arch_load_idt(const IDT_POINTER *pointer);
extern void arch_load_tr(void);
extern void arch_enter_user(uint64_t entry, uint64_t stack)
    __attribute__((noreturn));
extern void ring3_syscall_entry(void);
extern char kernel_stack_top[];
extern char user_demo_entry[];
extern char user_demo_page[];

static uint64_t gdt[7] __attribute__((aligned(16)));
static TSS64 tss __attribute__((aligned(16)));
static IDT_GATE idt[256] __attribute__((aligned(16)));

static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t page_directories[512][512]
    __attribute__((aligned(4096)));
static uint64_t user_page_table[512] __attribute__((aligned(4096)));
static uint8_t user_stack_page[4096] __attribute__((aligned(4096)));

static void set_tss_descriptor(uint64_t base, uint32_t limit)
{
    gdt[5] = ((uint64_t)(limit & 0xffffU)) |
             ((base & 0xffffffULL) << 16) |
             (0x89ULL << 40) |
             (((uint64_t)(limit >> 16) & 0xfULL) << 48) |
             (((base >> 24) & 0xffULL) << 56);
    gdt[6] = base >> 32;
}

static void set_idt_gate(unsigned int vector, void (*handler)(void),
                         uint8_t attributes)
{
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].attributes = attributes;
    idt[vector].offset_middle = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

static void setup_gdt_tss(void)
{
    DESCRIPTOR_POINTER pointer;

    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffULL;
    gdt[2] = 0x00cf92000000ffffULL;
    gdt[3] = 0x00cff2000000ffffULL;
    gdt[4] = 0x00affa000000ffffULL;

    tss.rsp0 = (uint64_t)(uintptr_t)kernel_stack_top;
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor((uint64_t)(uintptr_t)&tss,
                       (uint32_t)sizeof(tss) - 1U);

    pointer.limit = (uint16_t)(sizeof(gdt) - 1U);
    pointer.base = (uint64_t)(uintptr_t)gdt;
    arch_load_gdt(&pointer);
    arch_load_tr();
}

static void setup_idt(void)
{
    IDT_POINTER pointer;
    unsigned int i;

    for (i = 0; i < 256; ++i)
        set_idt_gate(i, ring3_syscall_entry, 0x8e);

    /* DPL 3 permits the user application to invoke int 0x80. */
    set_idt_gate(0x80, ring3_syscall_entry, 0xee);
    pointer.limit = (uint16_t)(sizeof(idt) - 1U);
    pointer.base = (uint64_t)(uintptr_t)idt;
    arch_load_idt(&pointer);
}

static void setup_user_page_tables(void)
{
    unsigned int i;
    unsigned int j;
    uint64_t code_physical =
        (uint64_t)(uintptr_t)user_demo_page;
    uint64_t stack_physical =
        (uint64_t)(uintptr_t)user_stack_page;

    for (i = 0; i < 512; ++i) {
        pml4[i] = 0;
        pdpt[i] = 0;
    }

    pml4[0] = (uint64_t)(uintptr_t)pdpt |
              PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    for (i = 0; i < 512; ++i) {
        pdpt[i] = (uint64_t)(uintptr_t)page_directories[i] |
                  PAGE_PRESENT | PAGE_WRITE |
                  (i == 1 ? PAGE_USER : 0);
        for (j = 0; j < 512; ++j) {
            uint64_t physical =
                (((uint64_t)i * 512ULL) + j) * 0x200000ULL;
            page_directories[i][j] = physical |
                PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;
        }
    }

    /* Replace the 1 GiB identity-map entry with 4 KiB mappings. */
    page_directories[1][0] =
        (uint64_t)(uintptr_t)user_page_table |
        PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    user_page_table[0] = (code_physical & ~0xfffULL) |
                         PAGE_PRESENT | PAGE_USER;
    user_page_table[1] = 0;
    user_page_table[2] = (stack_physical & ~0xfffULL) |
                         PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_NX;

    /* Enable NX, then switch to the kernel-owned identity page tables. */
    {
        uint32_t low, high;
        uint64_t efer;
        __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080));
        efer = ((uint64_t)high << 32) | low;
        efer |= (1ULL << 11);
        low = (uint32_t)efer;
        high = (uint32_t)(efer >> 32);
        __asm__ __volatile__("wrmsr" : : "a"(low), "d"(high), "c"(0xc0000080));
    }

    {
        uint64_t cr0;
        __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= (1ULL << 16); /* Supervisor writes respect read-only pages. */
        __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }

    __asm__ __volatile__(
        "mov %0, %%cr3"
        :
        : "r"((uint64_t)(uintptr_t)pml4)
        : "memory"
    );
}

void syscall_dispatch(SYSCALL_FRAME *frame)
{
    if (frame->rax == 1) {
        const char *message = (const char *)(uintptr_t)frame->rdi;
        size_t length = (size_t)frame->rsi;
        size_t i;

        if (message < (const char *)(uintptr_t)USER_BASE ||
            length > 256 ||
            (uintptr_t)message + length > USER_BASE + 4096ULL) {
            framebuffer_write("[SYSCALL] Rejected invalid user buffer\n");
            return;
        }

        for (i = 0; i < length; ++i)
            framebuffer_putc(message[i]);
        return;
    }

    if (frame->rax == 0) {
        framebuffer_write("[APP] Process exited with status ");
        framebuffer_write_dec(frame->rdi);
        framebuffer_write("\n");
        framebuffer_write("[MT] Ring 3 execution completed.\n");
        for (;;)
            __asm__ __volatile__("cli; hlt");
    }

    framebuffer_write("[SYSCALL] Unknown system call\n");
}

void usermode_run_demo(void)
{
    framebuffer_write("[RING3] Installing GDT and TSS...\n");
    setup_gdt_tss();
    framebuffer_write("[RING3] Installing IDT...\n");
    setup_idt();
    framebuffer_write("[RING3] Creating isolated user mappings...\n");
    setup_user_page_tables();
    framebuffer_write("[RING3] Entering CPL 3 at ");
    framebuffer_write_hex(USER_CODE);
    framebuffer_write("\n");
    arch_enter_user(USER_CODE,
                    USER_STACK + sizeof(user_stack_page) - 16U);
}
