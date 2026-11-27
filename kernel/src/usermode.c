/*
 * CookieOS Server 2026 - x86-64 Ring 3 ELF application loader
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
#define USER_PAGES   511U
#define USER_LIMIT   (USER_BASE + (uint64_t)USER_PAGES * 4096ULL)
#define USER_STACK   (USER_BASE + 511ULL * 4096ULL)
#define ELF_PT_LOAD  1U
#define ELF_PF_W     2U
#define ELF_PF_X     1U

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
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3, iomap_base;
} TSS64;
typedef struct __attribute__((packed)) {
    uint16_t offset_low, selector;
    uint8_t ist, attributes;
    uint16_t offset_middle;
    uint32_t offset_high, reserved;
} IDT_GATE;
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
} SYSCALL_FRAME;
typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ELF64_HEADER;
typedef struct __attribute__((packed)) {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} ELF64_PROGRAM;

extern void arch_load_gdt(const DESCRIPTOR_POINTER *pointer);
extern void arch_load_idt(const IDT_POINTER *pointer);
extern void arch_load_tr(void);
extern void arch_enter_user(uint64_t entry, uint64_t stack) __attribute__((noreturn));
extern void ring3_syscall_entry(void);
extern void ring3_fatal_exception(void);

static uint64_t gdt[7] __attribute__((aligned(16)));
static TSS64 tss __attribute__((aligned(16)));
static IDT_GATE idt[256] __attribute__((aligned(16)));
static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t page_directories[512][512] __attribute__((aligned(4096)));
static uint64_t user_page_table[512] __attribute__((aligned(4096)));
static uint8_t user_app_memory[USER_PAGES * 4096U] __attribute__((aligned(4096)));
static uint8_t user_stack_page[4096] __attribute__((aligned(4096)));
static uint64_t user_page_flags[USER_PAGES];
static uint8_t ring3_kernel_stack[16384] __attribute__((aligned(16)));
static uint64_t user_entry;
static uint64_t user_image_end;

static void set_tss_descriptor(uint64_t base, uint32_t limit)
{
    gdt[5] = ((uint64_t)(limit & 0xffffU)) |
             ((base & 0xffffffULL) << 16) | (0x89ULL << 40) |
             (((uint64_t)(limit >> 16) & 0xfULL) << 48) |
             (((base >> 24) & 0xffULL) << 56);
    gdt[6] = base >> 32;
}
static void set_idt_gate(unsigned int vector, void (*handler)(void), uint8_t attr)
{
    uint64_t a=(uint64_t)(uintptr_t)handler;
    idt[vector].offset_low=(uint16_t)a; idt[vector].selector=0x08;
    idt[vector].ist=0; idt[vector].attributes=attr;
    idt[vector].offset_middle=(uint16_t)(a>>16);
    idt[vector].offset_high=(uint32_t)(a>>32); idt[vector].reserved=0;
}
static void setup_gdt_tss(void)
{
    DESCRIPTOR_POINTER p;
    gdt[0]=0; gdt[1]=0x00af9a000000ffffULL; gdt[2]=0x00cf92000000ffffULL;
    gdt[3]=0x00cff2000000ffffULL; gdt[4]=0x00affa000000ffffULL;
    tss.rsp0=(uint64_t)(uintptr_t)(ring3_kernel_stack+sizeof(ring3_kernel_stack));
    tss.iomap_base=sizeof(tss);
    set_tss_descriptor((uint64_t)(uintptr_t)&tss,(uint32_t)sizeof(tss)-1U);
    p.limit=(uint16_t)(sizeof(gdt)-1U); p.base=(uint64_t)(uintptr_t)gdt;
    arch_load_gdt(&p); arch_load_tr();
}
static void setup_idt(void)
{
    IDT_POINTER p; unsigned i;
    for(i=0;i<256;i++)set_idt_gate(i,ring3_fatal_exception,0x8e);
    set_idt_gate(0x80,ring3_syscall_entry,0xee);
    p.limit=(uint16_t)(sizeof(idt)-1U);p.base=(uint64_t)(uintptr_t)idt;
    arch_load_idt(&p);
}
static int load_elf_app(const void *image,size_t size)
{
    const uint8_t *b=(const uint8_t*)image;
    const ELF64_HEADER *h;
    uint64_t high=0; int entry_executable=0; unsigned i;
    if(!image||size<sizeof(ELF64_HEADER))return 0;
    h=(const ELF64_HEADER*)b;
    if(b[0]!=0x7f||b[1]!='E'||b[2]!='L'||b[3]!='F'||
       b[4]!=2||b[5]!=1||h->type!=2||h->machine!=62||
       h->version!=1||h->phentsize!=sizeof(ELF64_PROGRAM)||
       !h->phnum||h->phnum>32)return 0;
    if(h->phoff>size||(uint64_t)h->phnum*sizeof(ELF64_PROGRAM)>size-h->phoff)return 0;
    for(i=0;i<h->phnum;i++){
        const ELF64_PROGRAM *p=(const ELF64_PROGRAM*)(b+h->phoff+(uint64_t)i*h->phentsize);
        uint64_t end,off;
        if(p->type!=ELF_PT_LOAD||!p->memsz)continue;
        if(p->filesz>p->memsz||p->offset>size||p->filesz>size-p->offset)return 0;
        if(p->vaddr<USER_BASE||p->vaddr>USER_LIMIT||p->memsz>USER_LIMIT-p->vaddr)return 0;
        end=p->vaddr+p->memsz;off=p->vaddr-USER_BASE;
        if(end>high)high=end;
        if((p->flags&ELF_PF_X)&&h->entry>=p->vaddr&&h->entry<end)
            entry_executable=1;
        {
            uint64_t page_start=off&~0xFFFULL;
            uint64_t page_end=(off+p->memsz+0xFFFULL)&~0xFFFULL;
            for(uint64_t page=page_start;page<page_end;page+=4096ULL){
                unsigned index=(unsigned)(page/4096ULL);
                if(index>=USER_PAGES)return 0;
                user_page_flags[index]=PAGE_PRESENT|PAGE_USER|
                    ((p->flags&ELF_PF_W)?PAGE_WRITE:0)|
                    ((p->flags&ELF_PF_X)?0:PAGE_NX);
            }
        }
        for(uint64_t j=0;j<p->filesz;j++)user_app_memory[off+j]=b[p->offset+j];
        if(end>USER_LIMIT)return 0;
    }
    if(high<=USER_BASE||h->entry<USER_BASE||h->entry>=high||!entry_executable)return 0;
    user_entry=h->entry;user_image_end=high;return 1;
}
static void setup_user_page_tables(void)
{
    unsigned i,j;
    for(i=0;i<512;i++){pml4[i]=0;pdpt[i]=0;}
    pml4[0]=(uint64_t)(uintptr_t)pdpt|PAGE_PRESENT|PAGE_WRITE|PAGE_USER;
    for(i=0;i<512;i++){
        pdpt[i]=(uint64_t)(uintptr_t)page_directories[i]|PAGE_PRESENT|PAGE_WRITE|(i==1?PAGE_USER:0);
        for(j=0;j<512;j++){
            uint64_t physical=(((uint64_t)i*512ULL)+j)*0x200000ULL;
            page_directories[i][j]=physical|PAGE_PRESENT|PAGE_WRITE|PAGE_HUGE;
        }
    }
    page_directories[1][0]=(uint64_t)(uintptr_t)user_page_table|PAGE_PRESENT|PAGE_WRITE|PAGE_USER;
    for(i=0;i<USER_PAGES;i++){
        uint64_t physical=(uint64_t)(uintptr_t)(user_app_memory+(size_t)i*4096U);
        user_page_table[i]=user_page_flags[i]
            ? physical|user_page_flags[i] : 0;
    }
    user_page_table[511]=((uint64_t)(uintptr_t)user_stack_page&~0xfffULL)|
                         PAGE_PRESENT|PAGE_WRITE|PAGE_USER|PAGE_NX;
    {
        uint32_t lo,hi;uint64_t efer;
        __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000080));
        efer=((uint64_t)hi<<32)|lo;efer|=1ULL<<11;
        lo=(uint32_t)efer;hi=(uint32_t)(efer>>32);
        __asm__ volatile("wrmsr"::"a"(lo),"d"(hi),"c"(0xc0000080));
    }
    {uint64_t cr0;__asm__ volatile("mov %%cr0,%0":"=r"(cr0));cr0|=1ULL<<16;__asm__ volatile("mov %0,%%cr0"::"r"(cr0):"memory");}
    __asm__ volatile("mov %0,%%cr3"::"r"((uint64_t)(uintptr_t)pml4):"memory");
}
void syscall_dispatch(SYSCALL_FRAME *f)
{
    if(f->rax==1){
        const char *s=(const char*)(uintptr_t)f->rdi;size_t n=(size_t)f->rsi;
        uintptr_t address=(uintptr_t)s;
        if(address<USER_BASE||n>4096U||address>user_image_end||
           n>user_image_end-address){
            framebuffer_write("[SYSCALL] Invalid user buffer\\n");return;
        }
        if(n){
            size_t first=(address-USER_BASE)/4096U;
            size_t last=(address+n-1U-USER_BASE)/4096U;
            if(last>=USER_PAGES){
                framebuffer_write("[SYSCALL] User buffer outside app memory\\n");return;
            }
            for(size_t page=first;page<=last;page++)
                if(!(user_page_table[page]&PAGE_PRESENT)){
                    framebuffer_write("[SYSCALL] Unmapped user buffer\\n");return;
                }
        }
        for(size_t i=0;i<n;i++)framebuffer_putc(s[i]);
        f->rax=0;return;
    }
    if(f->rax==0){
        framebuffer_write("[APP] Process exited with status ");framebuffer_write_dec(f->rdi);
        framebuffer_write("\n[MT] autoexec.app finished.\n");
        for(;;)__asm__ volatile("cli; hlt");
    }
    framebuffer_write("[SYSCALL] Unknown system call\n");f->rax=(uint64_t)-1;
}
void usermode_run_app(const void *image,size_t size)
{
    framebuffer_write("[RING3] Validating external ELF64 application...\n");
    for(size_t i=0;i<sizeof(user_app_memory);i++)user_app_memory[i]=0;
    if(!load_elf_app(image,size)){
        framebuffer_write("[APP] Invalid or unsupported ELF64 .app image.\n");
        for(;;)__asm__ volatile("cli; hlt");
    }
    setup_gdt_tss();setup_idt();setup_user_page_tables();
    framebuffer_write("[RING3] Starting external app at ");framebuffer_write_hex(user_entry);
    framebuffer_write("\n");
    arch_enter_user(user_entry,USER_BASE+512ULL*4096ULL-16ULL);
}
