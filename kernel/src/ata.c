/* CookieOS Server 2026 - primary ATA PIO driver
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "ata.h"
#include <stdint.h>
#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_ALTSTATUS 0x3F6
#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ 0x20
static inline void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t inb(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline uint16_t inw(uint16_t p){uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static void delay400(void){(void)inb(ATA_ALTSTATUS);(void)inb(ATA_ALTSTATUS);(void)inb(ATA_ALTSTATUS);(void)inb(ATA_ALTSTATUS);}
static int wait_not_busy(void){uint32_t n=1000000;uint8_t s;while(n--){s=inb(ATA_STATUS);if(s==0||s==0xFF)return 0;if(!(s&ATA_SR_BSY))return 1;}return 0;}
static int wait_data(void){uint32_t n=1000000;uint8_t s;while(n--){s=inb(ATA_STATUS);if(s&ATA_SR_ERR)return 0;if(s==0||s==0xFF)return 0;if(!(s&ATA_SR_BSY)&&(s&ATA_SR_DRQ))return 1;}return 0;}
int ata_init(void){uint16_t identify[256];unsigned i;if(!wait_not_busy())return 0;outb(ATA_DRIVE,0xA0);delay400();outb(ATA_SECCOUNT,0);outb(ATA_LBA0,0);outb(ATA_LBA1,0);outb(ATA_LBA2,0);outb(ATA_COMMAND,ATA_CMD_IDENTIFY);if(inb(ATA_STATUS)==0)return 0;if(!wait_not_busy())return 0;if(inb(ATA_LBA1)||inb(ATA_LBA2))return 0;if(!wait_data())return 0;for(i=0;i<256;i++)identify[i]=inw(ATA_DATA);return identify[49]&(1U<<9);}
int ata_read_sector(uint32_t lba,void *buffer){uint16_t *dst=(uint16_t*)buffer;unsigned i;if(!buffer||lba>=0x10000000U||!wait_not_busy())return 0;outb(ATA_DRIVE,(uint8_t)(0xE0U|((lba>>24)&0x0FU)));delay400();outb(ATA_SECCOUNT,1);outb(ATA_LBA0,(uint8_t)lba);outb(ATA_LBA1,(uint8_t)(lba>>8));outb(ATA_LBA2,(uint8_t)(lba>>16));outb(ATA_COMMAND,ATA_CMD_READ);if(!wait_data())return 0;for(i=0;i<256;i++)dst[i]=inw(ATA_DATA);return 1;}
