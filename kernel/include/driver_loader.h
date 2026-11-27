/*
 * CookieOS Server 2026 - ELF64 driver loader
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_DRIVER_LOADER_H
#define COOKIEOS_DRIVER_LOADER_H

#include <bootinfo.h>

void driver_load_all(const COOKIEOS_BOOT_INFO *boot_info);

#endif
