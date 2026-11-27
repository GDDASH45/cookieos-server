/*
 * CookieOS Server 2026 - native driver ABI
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef COOKIEOS_DRIVER_H
#define COOKIEOS_DRIVER_H

#define COOKIEOS_DRIVER_ABI_VERSION 1U
#define COOKIEOS_DRIVER_MAGIC 0x434B4452U

typedef struct {
    unsigned int magic;
    unsigned int abi_version;
    const char *name;
    const char *version;
} COOKIEOS_DRIVER_INFO;

/*
 * Every driver must export these entry points:
 *
 * int driver_init(void): return 0 on success, nonzero on failure.
 * void driver_exit(void): release resources acquired by driver_init().
 */
int driver_init(void);
void driver_exit(void);

/*
 * Place one metadata record in the .driver_info ELF section.
 * The future kernel module loader will validate this record.
 */
#define COOKIEOS_DRIVER_METADATA(driver_name, driver_version) \
    __attribute__((used, section(".driver_info"))) \
    const COOKIEOS_DRIVER_INFO cookieos_driver_info = { \
        COOKIEOS_DRIVER_MAGIC, \
        COOKIEOS_DRIVER_ABI_VERSION, \
        (driver_name), \
        (driver_version) \
    }

#endif
