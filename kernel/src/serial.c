/*
 * CookieOS Server 2026 - COM1 serial console
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "serial.h"

#define COM1 0x3F8

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c)
{
    unsigned int timeout = 1000000;

    while (!(inb(COM1 + 5) & 0x20) && --timeout)
        __asm__ __volatile__("pause");

    if (timeout)
        outb(COM1, (unsigned char)c);
}

void serial_write(const char *text)
{
    if (text == 0)
        return;

    while (*text) {
        if (*text == '\n')
            serial_putc('\r');
        serial_putc(*text++);
    }
}

void serial_write_hex(unsigned long long value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    serial_write("0x");

    for (shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xF]);
}

void serial_write_dec(unsigned long long value)
{
    char buffer[21];
    unsigned int count = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }

    while (value && count < sizeof(buffer)) {
        buffer[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count)
        serial_putc(buffer[--count]);
}