#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include <stddef.h>

#define AHCI_MAX_PORTS        32
#define AHCI_SECTOR_SIZE      512
#define AHCI_CMD_SLOTS        32

#define AHCI_SIG_ATA          0x00000101
#define AHCI_SIG_ATAPI        0xEB140101
#define AHCI_SIG_SEMB         0xC33C0101
#define AHCI_SIG_PM           0x96690101

#define AHCI_DEV_NULL         0
#define AHCI_DEV_SATA         1
#define AHCI_DEV_SATAPI       2
#define AHCI_DEV_SEMB         3
#define AHCI_DEV_PM           4

#define HBA_PORT_IPM_ACTIVE   1
#define HBA_PORT_DET_PRESENT  3

#define HBA_PxCMD_ST          0x0001
#define HBA_PxCMD_FRE         0x0010
#define HBA_PxCMD_FR          0x4000
#define HBA_PxCMD_CR          0x8000

#define HBA_PxIS_TFES         (1 << 30)

#define HBA_GHC_AE            (1U << 31)
#define HBA_GHC_HR            (1 << 0)

#define ATA_CMD_READ_DMA_EX   0x25
#define ATA_CMD_WRITE_DMA_EX  0x35
#define ATA_CMD_IDENTIFY      0xEC
#define ATA_DEV_BUSY          0x80
#define ATA_DEV_DRQ           0x08

#define FIS_TYPE_REG_H2D      0x27
#define FIS_TYPE_REG_D2H      0x34
#define FIS_TYPE_DMA_ACT      0x39
#define FIS_TYPE_DMA_SETUP    0x41
#define FIS_TYPE_DATA         0x46
#define FIS_TYPE_BIST         0x58
#define FIS_TYPE_PIO_SETUP    0x5F
#define FIS_TYPE_DEV_BITS     0xA1

#define PCI_CONFIG_ADDR       0x0CF8
#define PCI_CONFIG_DATA       0x0CFC
#define PCI_CLASS_STORAGE     0x01
#define PCI_SUBCLASS_AHCI     0x06

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t rsv0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef volatile struct {
    uint32_t   cap;
    uint32_t   ghc;
    uint32_t   is;
    uint32_t   pi;
    uint32_t   vs;
    uint32_t   ccc_ctl;
    uint32_t   ccc_pts;
    uint32_t   em_loc;
    uint32_t   em_ctl;
    uint32_t   cap2;
    uint32_t   bohc;
    uint8_t    rsv[0xA0 - 0x2C];
    uint8_t    vendor[0x100 - 0xA0];
    hba_port_t ports[];
} hba_mem_t;

typedef struct {
    uint8_t  cfl : 5;
    uint8_t  a : 1;
    uint8_t  w : 1;
    uint8_t  p : 1;
    uint8_t  r : 1;
    uint8_t  b : 1;
    uint8_t  c : 1;
    uint8_t  rsv0 : 1;
    uint8_t  pmp : 4;
    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv1[4];
} hba_cmd_header_t;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc : 22;
    uint32_t rsv1 : 9;
    uint32_t i : 1;
} hba_prdt_entry_t;

typedef struct {
    uint8_t fis_type;
    uint8_t pmport : 4;
    uint8_t rsv0 : 3;
    uint8_t c : 1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t rsv1[4];
} fis_reg_h2d_t;

typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    hba_prdt_entry_t prdt_entry[];
} hba_cmd_tbl_t;

void ahci_init(void);

#endif
