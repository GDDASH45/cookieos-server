/*
 * CookieOS Server 2026 - serial console
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef COOKIEOS_SERIAL_H
#define COOKIEOS_SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *text);
void serial_write_hex(unsigned long long value);
void serial_write_dec(unsigned long long value);

#endif