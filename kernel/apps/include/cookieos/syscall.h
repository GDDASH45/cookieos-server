/* CookieOS Server 2026 userspace syscall ABI
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_SYSCALL_H
#define COOKIEOS_SYSCALL_H
#include <stddef.h>
static inline long cookieos_write(const char *text,size_t length){long r=1;__asm__ volatile("int $0x80":"+a"(r):"D"(text),"S"(length):"memory","rcx","r11");return r;}
__attribute__((noreturn)) static inline void cookieos_exit(unsigned long status){long r=0;__asm__ volatile("int $0x80":"+a"(r):"D"(status):"memory","rcx","r11");for(;;)__asm__ volatile("hlt");}
#endif
