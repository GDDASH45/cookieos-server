/* CookieOS Server 2026 - AHCI SATA block device
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef COOKIEOS_ATA_H
#define COOKIEOS_ATA_H
#include <stdint.h>
int ata_init(uint64_t kernel_physical_base, uint64_t kernel_size);
int ata_read_sector(uint32_t lba, void *buffer);
#endif
