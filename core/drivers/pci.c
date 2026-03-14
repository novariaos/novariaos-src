// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/pci.h>
#include <core/arch/io.h>

static uint32_t pci_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (1u << 31)
        | ((uint32_t)bus         << 16)
        | ((uint32_t)(dev & 0x1F) << 11)
        | ((uint32_t)(func & 0x7) << 8)
        | (offset & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, dev, func, offset & ~3u);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, dev, func, offset & ~3u);
    return (uint8_t)(val >> ((offset & 3) * 8));
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_addr(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t cur   = pci_read32(bus, dev, func, offset & ~3u);
    uint32_t shift = (offset & 2) * 8;
    cur = (cur & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, dev, func, offset & ~3u, cur);
}

bool pci_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                               uint8_t* bus_out, uint8_t* dev_out, uint8_t* func_out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            if (pci_read32((uint8_t)bus, dev, 0, 0) == 0xFFFFFFFF) continue;

            uint8_t hdr      = pci_read8((uint8_t)bus, dev, 0, 0x0E) & 0x7F;
            uint8_t max_func = (hdr == 0) ? 1 : 8;

            for (uint8_t func = 0; func < max_func; func++) {
                if (func > 0 && pci_read32((uint8_t)bus, dev, func, 0) == 0xFFFFFFFF)
                    continue;

                uint32_t class_reg = pci_read32((uint8_t)bus, dev, func, 0x08);
                if (((class_reg >> 24) & 0xFF) == class_code &&
                    ((class_reg >> 16) & 0xFF) == subclass   &&
                    ((class_reg >>  8) & 0xFF) == prog_if) {
                    *bus_out  = (uint8_t)bus;
                    *dev_out  = dev;
                    *func_out = func;
                    return true;
                }
            }
        }
    }
    return false;
}

bool pci_find_capability(uint8_t bus, uint8_t dev, uint8_t func,
                         uint8_t cap_id, uint8_t* cap_offset) {
    uint16_t status = pci_read16(bus, dev, func, 0x06);
    if (!(status & (1 << 4))) return false;

    uint8_t ptr = pci_read8(bus, dev, func, 0x34) & 0xFC;
    for (int i = 0; i < 48 && ptr >= 0x40; i++) {
        if (pci_read8(bus, dev, func, ptr) == cap_id) {
            *cap_offset = ptr;
            return true;
        }
        ptr = pci_read8(bus, dev, func, ptr + 1) & 0xFC;
    }
    return false;
}

void pci_enable_msi(uint8_t bus, uint8_t dev, uint8_t func,
                    uint64_t msi_addr, uint32_t msi_data) {
    uint8_t cap;
    if (!pci_find_capability(bus, dev, func, PCI_CAP_MSI, &cap)) return;

    uint16_t ctrl  = pci_read16(bus, dev, func, cap + 2);
    bool     is64  = (ctrl >> 7) & 1;

    pci_write32(bus, dev, func, cap + 4, (uint32_t)(msi_addr & 0xFFFFFFFF));
    if (is64) {
        pci_write32(bus, dev, func, cap + 8,  (uint32_t)(msi_addr >> 32));
        pci_write32(bus, dev, func, cap + 12, msi_data);
    } else {
        pci_write32(bus, dev, func, cap + 8, msi_data);
    }

    ctrl = (ctrl & ~(uint16_t)0x70u) | 1u;
    pci_write16(bus, dev, func, cap + 2, ctrl);
}
