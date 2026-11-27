/* CookieOS Server 2026 - FAT filesystem reader
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_FAT_H
#define COOKIEOS_FAT_H
#include <stddef.h>
int fat_init(uint64_t kernel_physical_base, uint64_t kernel_size);
int fat_read_file(const char *path, void *buffer, size_t capacity, size_t *size);
#endif
