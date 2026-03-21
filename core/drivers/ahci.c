// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/ahci.h>
#include <core/fs/block.h>
#include <core/kernel/mem/allocator.h>
#include <core/arch/io.h>
#include <log.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

static hba_mem_t* abar = NULL;

static inline uint64_t virt_to_phys(void* virt) {
    return (uint64_t)(uintptr_t)virt - get_hhdm_offset();
}

static inline void* phys_to_virt(uint64_t phys) {
    return (void*)(uintptr_t)(phys + get_hhdm_offset());
}

typedef struct {
    hba_port_t*      port;
    hba_cmd_header_t* cmd_list;
    void*            fis_base;
    hba_cmd_tbl_t*   cmd_tbl[AHCI_CMD_SLOTS];
    uint64_t         sector_count;
    int              port_num;
} ahci_port_t;

#define AHCI_MAX_DEVICES 8
static ahci_port_t ahci_devices[AHCI_MAX_DEVICES];
static int ahci_device_count = 0;

static inline void mmio_write32(volatile uint32_t* addr, uint32_t val) {
    *addr = val;
    __asm__ volatile("" ::: "memory");
}

static inline uint32_t mmio_read32(volatile uint32_t* addr) {
    uint32_t val = *addr;
    __asm__ volatile("" ::: "memory");
    return val;
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static uint64_t pci_find_ahci(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read32(bus, slot, func, 0x00);
                if (id == 0xFFFFFFFF || id == 0)
                    continue;

                uint32_t class_reg = pci_read32(bus, slot, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass   = (class_reg >> 16) & 0xFF;

                if (class_code == PCI_CLASS_STORAGE && subclass == PCI_SUBCLASS_AHCI) {
                    uint32_t bar5 = pci_read32(bus, slot, func, 0x24);

                    uint32_t cmd = pci_read32(bus, slot, func, 0x04);
                    cmd |= (1 << 1) | (1 << 2);
                    pci_write32(bus, slot, func, 0x04, cmd);

                    return (uint64_t)(bar5 & 0xFFFFF000);
                }

                if (func == 0) {
                    uint32_t hdr = pci_read32(bus, slot, func, 0x0C);
                    if (!((hdr >> 16) & 0x80))
                        break;
                }
            }
        }
    }
    return 0;
}

static int ahci_port_type(hba_port_t* port) {
    uint32_t ssts = mmio_read32(&port->ssts);
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE)
        return AHCI_DEV_NULL;

    uint32_t sig = mmio_read32(&port->sig);
    switch (sig) {
        case AHCI_SIG_ATAPI: return AHCI_DEV_SATAPI;
        case AHCI_SIG_SEMB:  return AHCI_DEV_SEMB;
        case AHCI_SIG_PM:    return AHCI_DEV_PM;
        default:             return AHCI_DEV_SATA;
    }
}

static void ahci_stop_cmd(hba_port_t* port) {
    uint32_t cmd = mmio_read32(&port->cmd);
    cmd &= ~HBA_PxCMD_ST;
    cmd &= ~HBA_PxCMD_FRE;
    mmio_write32(&port->cmd, cmd);

    for (int i = 0; i < 500000; i++) {
        uint32_t c = mmio_read32(&port->cmd);
        if (!(c & HBA_PxCMD_FR) && !(c & HBA_PxCMD_CR))
            return;
    }
}

static void ahci_start_cmd(hba_port_t* port) {
    for (int i = 0; i < 500000; i++) {
        if (!(mmio_read32(&port->cmd) & HBA_PxCMD_CR))
            break;
    }

    uint32_t cmd = mmio_read32(&port->cmd);
    cmd |= HBA_PxCMD_FRE;
    cmd |= HBA_PxCMD_ST;
    mmio_write32(&port->cmd, cmd);
}

static int ahci_find_free_slot(hba_port_t* port) {
    uint32_t slots = mmio_read32(&port->sact) | mmio_read32(&port->ci);
    for (int i = 0; i < AHCI_CMD_SLOTS; i++) {
        if (!(slots & (1 << i)))
            return i;
    }
    return -1;
}

static void ahci_port_rebase(ahci_port_t* dev) {
    hba_port_t* port = dev->port;

    ahci_stop_cmd(port);

    void* cmd_base = kmalloc(1024);
    memset(cmd_base, 0, 1024);
    dev->cmd_list = (hba_cmd_header_t*)cmd_base;
    uint64_t cmd_phys = virt_to_phys(cmd_base);
    mmio_write32(&port->clb, (uint32_t)cmd_phys);
    mmio_write32(&port->clbu, (uint32_t)(cmd_phys >> 32));

    void* fis = kmalloc(256);
    memset(fis, 0, 256);
    dev->fis_base = fis;
    uint64_t fis_phys = virt_to_phys(fis);
    mmio_write32(&port->fb, (uint32_t)fis_phys);
    mmio_write32(&port->fbu, (uint32_t)(fis_phys >> 32));

    hba_cmd_header_t* hdr = dev->cmd_list;
    for (int i = 0; i < AHCI_CMD_SLOTS; i++) {
        hdr[i].prdtl = 8;
        void* tbl = kmalloc(256 + 8 * sizeof(hba_prdt_entry_t));
        memset(tbl, 0, 256 + 8 * sizeof(hba_prdt_entry_t));
        dev->cmd_tbl[i] = (hba_cmd_tbl_t*)tbl;
        uint64_t tbl_phys = virt_to_phys(tbl);
        hdr[i].ctba  = (uint32_t)tbl_phys;
        hdr[i].ctbau = (uint32_t)(tbl_phys >> 32);
    }

    mmio_write32(&port->serr, 0xFFFFFFFF);
    mmio_write32(&port->is, 0xFFFFFFFF);

    ahci_start_cmd(port);
}

static int ahci_issue_cmd(ahci_port_t* dev, int slot) {
    hba_port_t* port = dev->port;

    mmio_write32(&port->ci, 1 << slot);

    for (int spin = 0; spin < 1000000; spin++) {
        if (!(mmio_read32(&port->ci) & (1 << slot)))
            return 0;
        if (mmio_read32(&port->is) & HBA_PxIS_TFES)
            return -1;
    }

    return -1;
}

static int ahci_identify(ahci_port_t* dev) {
    hba_port_t* port = dev->port;

    mmio_write32(&port->is, 0xFFFFFFFF);

    int slot = ahci_find_free_slot(port);
    if (slot < 0) return -1;

    hba_cmd_header_t* hdr = &dev->cmd_list[slot];
    hdr->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    hdr->w = 0;
    hdr->prdtl = 1;

    hba_cmd_tbl_t* tbl = dev->cmd_tbl[slot];
    memset(tbl, 0, sizeof(hba_cmd_tbl_t) + sizeof(hba_prdt_entry_t));

    uint8_t* ident_buf = kmalloc(512);
    if (!ident_buf) return -1;
    memset(ident_buf, 0, 512);

    uint64_t ident_phys = virt_to_phys(ident_buf);
    tbl->prdt_entry[0].dba  = (uint32_t)ident_phys;
    tbl->prdt_entry[0].dbau = (uint32_t)(ident_phys >> 32);
    tbl->prdt_entry[0].dbc  = 511;
    tbl->prdt_entry[0].i    = 1;

    fis_reg_h2d_t* fis = (fis_reg_h2d_t*)tbl->cfis;
    memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c        = 1;
    fis->command  = ATA_CMD_IDENTIFY;
    fis->device   = 0;

    if (ahci_issue_cmd(dev, slot) < 0) {
        kfree(ident_buf);
        return -1;
    }

    uint64_t* lba48 = (uint64_t*)(ident_buf + 200);
    dev->sector_count = *lba48;

    if (dev->sector_count == 0) {
        uint32_t* lba28 = (uint32_t*)(ident_buf + 120);
        dev->sector_count = *lba28;
    }

    kfree(ident_buf);
    return 0;
}

static int ahci_rw(ahci_port_t* dev, uint64_t lba, size_t count, void* buf, bool write) {
    hba_port_t* port = dev->port;
    uint8_t* ptr = (uint8_t*)buf;

    while (count > 0) {
        size_t chunk = (count > 128) ? 128 : count;

        mmio_write32(&port->is, 0xFFFFFFFF);

        int slot = ahci_find_free_slot(port);
        if (slot < 0) return -1;

        hba_cmd_header_t* hdr = &dev->cmd_list[slot];
        hdr->cfl   = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
        hdr->w     = write ? 1 : 0;
        hdr->c     = 1;
        hdr->prdtl = 0;

        hba_cmd_tbl_t* tbl = dev->cmd_tbl[slot];

        size_t bytes_left = chunk * AHCI_SECTOR_SIZE;
        uint8_t* p = ptr;
        int prdt_idx = 0;

        while (bytes_left > 0) {
            size_t seg = (bytes_left > 0x400000) ? 0x400000 : bytes_left;
            uint64_t p_phys = virt_to_phys(p);
            tbl->prdt_entry[prdt_idx].dba  = (uint32_t)p_phys;
            tbl->prdt_entry[prdt_idx].dbau = (uint32_t)(p_phys >> 32);
            tbl->prdt_entry[prdt_idx].dbc  = (uint32_t)(seg - 1);
            tbl->prdt_entry[prdt_idx].i    = (bytes_left - seg == 0) ? 1 : 0;
            bytes_left -= seg;
            p += seg;
            prdt_idx++;
        }

        hdr->prdtl = (uint16_t)prdt_idx;

        fis_reg_h2d_t* fis = (fis_reg_h2d_t*)tbl->cfis;
        memset(fis, 0, sizeof(fis_reg_h2d_t));
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c        = 1;
        fis->command  = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
        fis->device   = 1 << 6;
        fis->lba0     = (uint8_t)(lba);
        fis->lba1     = (uint8_t)(lba >> 8);
        fis->lba2     = (uint8_t)(lba >> 16);
        fis->lba3     = (uint8_t)(lba >> 24);
        fis->lba4     = (uint8_t)(lba >> 32);
        fis->lba5     = (uint8_t)(lba >> 40);
        fis->countl   = (uint8_t)(chunk);
        fis->counth   = (uint8_t)(chunk >> 8);

        if (ahci_issue_cmd(dev, slot) < 0)
            return -1;

        ptr   += chunk * AHCI_SECTOR_SIZE;
        lba   += chunk;
        count -= chunk;
    }

    return 0;
}

static int ahci_read_blocks(struct block_device* dev, uint64_t lba, size_t count, void* buf) {
    ahci_port_t* ap = (ahci_port_t*)dev->private_data;
    if (lba + count > ap->sector_count)
        return -1;
    return ahci_rw(ap, lba, count, buf, false);
}

static int ahci_write_blocks(struct block_device* dev, uint64_t lba, size_t count, const void* buf) {
    ahci_port_t* ap = (ahci_port_t*)dev->private_data;
    if (lba + count > ap->sector_count)
        return -1;
    return ahci_rw(ap, lba, count, (void*)buf, true);
}

void ahci_init(void) {
    uint64_t bar5 = pci_find_ahci();
    if (!bar5) {
        LOG_DEBUG("AHCI: No controller found\n");
        return;
    }

    abar = (hba_mem_t*)phys_to_virt(bar5);

    uint32_t ghc = mmio_read32(&abar->ghc);
    ghc |= HBA_GHC_AE;
    mmio_write32(&abar->ghc, ghc);

    uint32_t vs = mmio_read32(&abar->vs);
    LOG_DEBUG("AHCI: Controller found, version %d.%d\n",
              (vs >> 16) & 0xFFFF, vs & 0xFFFF);

    uint32_t pi = mmio_read32(&abar->pi);
    int dev_idx = 0;

    for (int i = 0; i < AHCI_MAX_PORTS && dev_idx < AHCI_MAX_DEVICES; i++) {
        if (!(pi & (1 << i)))
            continue;

        hba_port_t* port = &abar->ports[i];
        int type = ahci_port_type(port);

        if (type != AHCI_DEV_SATA)
            continue;

        ahci_port_t* ap = &ahci_devices[dev_idx];
        ap->port     = port;
        ap->port_num = i;

        ahci_port_rebase(ap);

        if (ahci_identify(ap) < 0) {
            LOG_WARN("AHCI: Failed to identify port %d\n", i);
            continue;
        }

        LOG_DEBUG("AHCI: Port %d — %llu sectors\n", i, ap->sector_count);

        char name[8];
        name[0] = 's'; name[1] = 'd';
        name[2] = 'a' + (char)dev_idx;
        name[3] = '\0';

        block_device_ops_t ops = {
            .read_blocks  = ahci_read_blocks,
            .write_blocks = ahci_write_blocks,
        };

        register_block_device(name, AHCI_SECTOR_SIZE, ap->sector_count, &ops, ap);
        dev_idx++;
    }

    ahci_device_count = dev_idx;

    if (dev_idx == 0)
        LOG_DEBUG("AHCI: No SATA devices found\n");
    else
        LOG_DEBUG("AHCI: %d device(s) initialized\n", dev_idx);
}
