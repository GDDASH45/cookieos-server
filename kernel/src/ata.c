/*
 * CookieOS Server 2026 - PCI AHCI SATA block driver
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "ata.h"
#include <stdint.h>

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define AHCI_CLASS 0x01
#define AHCI_SUBCLASS 0x06
#define AHCI_PROGIF 0x01
#define HBA_GHC 0x04
#define HBA_PI 0x0C
#define HBA_PORT_BASE 0x100
#define HBA_PORT_SIZE 0x80
#define PORT_CLB 0x00
#define PORT_FB 0x08
#define PORT_IS 0x10
#define PORT_CMD 0x18
#define PORT_TFD 0x20
#define PORT_SIG 0x24
#define PORT_SSTS 0x28
#define PORT_SERR 0x30
#define PORT_CI 0x38
#define CMD_ST 0x0001
#define CMD_FRE 0x0010
#define CMD_FR 0x4000
#define CMD_CR 0x8000
#define HBA_AE 0x80000000U
#define ATA_READ_DMA_EXT 0x25
#define SATA_SIG_ATA 0x00000101U

typedef struct __attribute__((packed,aligned(1024))) {
    uint8_t bytes[1024];
} AHCI_COMMAND_LIST;
typedef struct __attribute__((packed,aligned(256))) {
    uint8_t bytes[256];
} AHCI_RECEIVED_FIS;
typedef struct __attribute__((packed,aligned(128))) {
    uint8_t bytes[256];
} AHCI_COMMAND_TABLE;

static AHCI_COMMAND_LIST command_list;
static AHCI_RECEIVED_FIS received_fis;
static AHCI_COMMAND_TABLE command_table;
static uint8_t dma_buffer[512] __attribute__((aligned(512)));
static volatile uint8_t *abar;
static volatile uint8_t *port;
static int initialized;

static inline void outl(uint16_t p,uint32_t v)
{
    __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));
}
static inline uint32_t inl(uint16_t p)
{
    uint32_t v;
    __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));
    return v;
}
static uint32_t pci_read(uint8_t bus,uint8_t dev,uint8_t fn,uint8_t reg)
{
    uint32_t address=0x80000000U|((uint32_t)bus<<16)|
        ((uint32_t)dev<<11)|((uint32_t)fn<<8)|(reg&0xFCU);
    outl(PCI_ADDR,address);
    return inl(PCI_DATA);
}
static void pci_write(uint8_t bus,uint8_t dev,uint8_t fn,uint8_t reg,uint32_t value)
{
    uint32_t address=0x80000000U|((uint32_t)bus<<16)|
        ((uint32_t)dev<<11)|((uint32_t)fn<<8)|(reg&0xFCU);
    outl(PCI_ADDR,address);
    outl(PCI_DATA,value);
}
static int find_ahci(uint8_t *bus_out,uint8_t *dev_out,uint8_t *fn_out,uint32_t *bar_out)
{
    unsigned bus,dev,fn;
    for(bus=0;bus<256;bus++)for(dev=0;dev<32;dev++){
        uint32_t id=pci_read((uint8_t)bus,(uint8_t)dev,0,0);
        if((id&0xFFFFU)==0xFFFFU)continue;
        for(fn=0;fn<8;fn++){
            uint32_t cls=pci_read((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,8);
            uint32_t code=cls>>8;
            if(((code>>16)&0xFFU)==AHCI_CLASS&&
               ((code>>8)&0xFFU)==AHCI_SUBCLASS&&
               (code&0xFFU)==AHCI_PROGIF){
                uint32_t bar=pci_read((uint8_t)bus,(uint8_t)dev,(uint8_t)fn,0x24);
                if((bar&1U)||!(bar&0xFFFFFFF0U))continue;
                *bus_out=(uint8_t)bus;*dev_out=(uint8_t)dev;
                *fn_out=(uint8_t)fn;*bar_out=bar&0xFFFFFFF0U;
                return 1;
            }
        }
    }
    return 0;
}
static uint32_t mmio_read(volatile uint8_t *base,uint32_t offset)
{
    return *(volatile uint32_t *)(base+offset);
}
static void mmio_write(volatile uint8_t *base,uint32_t offset,uint32_t value)
{
    *(volatile uint32_t *)(base+offset)=value;
}
static int wait_clear(volatile uint8_t *p,uint32_t reg,uint32_t mask)
{
    uint32_t n=10000000;
    while(n--)if(!(mmio_read(p,reg)&mask))return 1;
    return 0;
}
int ata_init(void)
{
    uint8_t bus,dev,fn;
    uint32_t bar,pi;
    unsigned i;
    if(initialized)return 1;
    if(!find_ahci(&bus,&dev,&fn,&bar))return 0;

    /* Enable PCI memory-space decoding and bus mastering. */
    {
        uint32_t command=pci_read(bus,dev,fn,4);
        pci_write(bus,dev,fn,4,(command&0xFFFFU)|0x00000006U);
    }
    abar=(volatile uint8_t *)(uintptr_t)bar;
    mmio_write(abar,HBA_GHC,mmio_read(abar,HBA_GHC)|HBA_AE);
    pi=mmio_read(abar,HBA_PI);
    for(i=0;i<32;i++){
        volatile uint8_t *candidate;
        uint32_t ssts,signature,cmd;
        if(!(pi&(1U<<i)))continue;
        candidate=abar+HBA_PORT_BASE+i*HBA_PORT_SIZE;
        ssts=mmio_read(candidate,PORT_SSTS);
        signature=mmio_read(candidate,PORT_SIG);
        if((ssts&0x0FU)!=3U||signature!=SATA_SIG_ATA)continue;
        port=candidate;
        cmd=mmio_read(port,PORT_CMD);
        mmio_write(port,PORT_CMD,cmd&~CMD_ST);
        if(!wait_clear(port,PORT_CMD,CMD_CR))return 0;
        cmd=mmio_read(port,PORT_CMD);
        mmio_write(port,PORT_CMD,cmd&~CMD_FRE);
        if(!wait_clear(port,PORT_CMD,CMD_FR))return 0;
        mmio_write(port,PORT_CLB,(uint32_t)(uintptr_t)&command_list);
        mmio_write(port,PORT_CLB+4,0);
        mmio_write(port,PORT_FB,(uint32_t)(uintptr_t)&received_fis);
        mmio_write(port,PORT_FB+4,0);
        mmio_write(port,PORT_IS,0xFFFFFFFFU);
        mmio_write(port,PORT_SERR,0xFFFFFFFFU);
        cmd=mmio_read(port,PORT_CMD);
        mmio_write(port,PORT_CMD,cmd|CMD_FRE);
        mmio_write(port,PORT_CMD,mmio_read(port,PORT_CMD)|CMD_ST);
        initialized=1;
        return 1;
    }
    return 0;
}
int ata_read_sector(uint32_t lba,void *buffer)
{
    volatile uint8_t *header;
    volatile uint8_t *table;
    volatile uint8_t *fis;
    uint32_t ci;
    unsigned i;
    if(!initialized||!buffer||lba>=0x10000000U)return 0;
    for(i=0;i<512;i++)dma_buffer[i]=0;

    header=(volatile uint8_t *)command_list.bytes;
    table=(volatile uint8_t *)command_table.bytes;
    fis=table;
    for(i=0;i<256;i++)table[i]=0;
    for(i=0;i<32;i++)header[i]=0;

    /* One command header, five DWORD FIS, one 512-byte PRDT entry. */
    header[0]=5;
    header[1]=0;
    header[2]=1;
    header[3]=0;
    *(volatile uint32_t *)(header+8)=(uint32_t)(uintptr_t)&command_table;
    *(volatile uint32_t *)(header+12)=0;

    fis[0]=0x27;             /* Register Host-to-Device FIS. */
    fis[1]=0x80;             /* Command bit. */
    fis[2]=ATA_READ_DMA_EXT;
    fis[4]=(uint8_t)lba;
    fis[5]=(uint8_t)(lba>>8);
    fis[6]=(uint8_t)(lba>>16);
    fis[7]=0x40;             /* LBA mode. */
    fis[8]=(uint8_t)(lba>>24);
    fis[9]=0;
    fis[10]=0;
    fis[12]=1;               /* Sector count low. */
    fis[13]=0;

    *(volatile uint32_t *)(table+0x80)=(uint32_t)(uintptr_t)dma_buffer;
    *(volatile uint32_t *)(table+0x84)=0;
    *(volatile uint32_t *)(table+0x8C)=511U|(1U<<31);

    {
        uint32_t timeout=10000000U;
        while(timeout-- && (mmio_read(port,PORT_TFD)&0x88U)) { }
        if(!timeout)return 0;
    }
    __asm__ volatile("" ::: "memory");
    mmio_write(port,PORT_IS,0xFFFFFFFFU);
    ci=mmio_read(port,PORT_CI);
    mmio_write(port,PORT_CI,ci|1U);
    {
        uint32_t timeout=10000000U;
        while(timeout--){
            if(mmio_read(port,PORT_IS)&(1U<<30))return 0;
            if(!(mmio_read(port,PORT_CI)&1U))break;
        }
        if(!timeout)return 0;
    }
    if(mmio_read(port,PORT_TFD)&0x01U)return 0;
    __asm__ volatile("" ::: "memory");
    for(i=0;i<512;i++)((uint8_t*)buffer)[i]=dma_buffer[i];
    return 1;
}
