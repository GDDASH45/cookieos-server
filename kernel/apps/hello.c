/* CookieOS Server 2026 - C userspace application example
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <cookieos/syscall.h>

void _start(void)
{
    static const char message[] = "[APP] Hello from a separately built C application!\n";
    cookieos_write(message, sizeof(message) - 1U);
    cookieos_exit(0);
}
