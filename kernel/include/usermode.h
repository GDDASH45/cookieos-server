/*
 * CookieOS Server 2026 - Ring 3 application support
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_USERMODE_H
#define COOKIEOS_USERMODE_H
#include <stddef.h>
void usermode_run_app(const void *image, size_t size) __attribute__((noreturn));
#endif
