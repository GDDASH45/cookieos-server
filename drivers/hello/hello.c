/*
 * CookieOS Server 2026 - example native driver
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <driver.h>

COOKIEOS_DRIVER_METADATA("Hello Driver", "1.0.0");

int driver_init(void)
{
    /*
     * Driver initialization will use kernel services once the
     * kernel driver API and ELF64 module loader are implemented.
     */
    return 0;
}

void driver_exit(void)
{
    /* No resources are allocated by this example driver. */
}
