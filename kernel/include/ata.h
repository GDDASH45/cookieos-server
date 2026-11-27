/* CookieOS Server 2026 - AHCI SATA block device
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_ATA_H
#define COOKIEOS_ATA_H
#include <stdint.h>
int ata_init(void);
int ata_read_sector(uint32_t lba, void *buffer);
#endif
