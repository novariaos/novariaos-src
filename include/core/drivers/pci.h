// SPDX-License-Identifier: GPL-3.0-only

#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

#define PCI_CAP_MSI      0x05

uint32_t pci_read32 (uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_read16 (uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t  pci_read8  (uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

void     pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val);

bool pci_find_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                               uint8_t* bus_out, uint8_t* dev_out, uint8_t* func_out);

bool pci_find_capability(uint8_t bus, uint8_t dev, uint8_t func,
                         uint8_t cap_id, uint8_t* cap_offset);

void pci_enable_msi(uint8_t bus, uint8_t dev, uint8_t func,
                    uint64_t msi_addr, uint32_t msi_data);

#endif
