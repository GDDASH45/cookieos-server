/*
 * CookieOS Server 2026 ELF loader interface
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_ELF_LOADER_H
#define COOKIEOS_ELF_LOADER_H

#include <efi.h>

EFI_STATUS load_kernel_and_exit(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable,
    const CHAR16 *Path
);

#endif
