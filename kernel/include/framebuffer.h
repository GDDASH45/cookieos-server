/*
 * CookieOS Server 2026 - framebuffer console
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_FRAMEBUFFER_H
#define COOKIEOS_FRAMEBUFFER_H

#include <stddef.h>
#include <bootinfo.h>

void framebuffer_init(const COOKIEOS_BOOT_INFO *info);
void framebuffer_clear(void);
int framebuffer_draw_bmp(const void *data, size_t size);
int framebuffer_available(void);
void framebuffer_putc(char c);
void framebuffer_write(const char *text);
void framebuffer_write_hex(unsigned long long value);
void framebuffer_write_dec(unsigned long long value);

#endif
