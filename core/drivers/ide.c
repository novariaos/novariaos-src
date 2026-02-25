// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/ide.h>
#include <core/fs/block.h>
#include <core/kernel/mem/allocator.h>
#include <core/kernel/log.h>
#include <core/arch/io.h>
#include <stdint.h>
#include <stddef.h>

// ATA register offsets from base port
#define ATA_REG_DATA        0   // R/W: 16-bit data
#define ATA_REG_ERROR       1   // R:   error flags
#define ATA_REG_FEATURES    1   // W:   features
#define ATA_REG_SECCOUNT    2   // R/W: sector count
#define ATA_REG_LBA_LO      3   // R/W: LBA bits 0-7
#define ATA_REG_LBA_MID     4   // R/W: LBA bits 8-15
#define ATA_REG_LBA_HI      5   // R/W: LBA bits 16-23
#define ATA_REG_DRIVE       6   // R/W: drive select + LBA bits 24-27
#define ATA_REG_STATUS      7   // R:   status
#define ATA_REG_CMD         7   // W:   command

// ATA status bits
#define ATA_SR_BSY  0x80    // Drive busy
#define ATA_SR_DRQ  0x08    // Data request ready
#define ATA_SR_ERR  0x01    // Error

// ATA commands
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ_PIO    0x20

// IDENTIFY data offsets (in 16-bit words)
#define ATA_IDENT_LBA28_SECTORS 60  // words 60-61: 28-bit LBA sector count

// ATA channel configs
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170
#define ATA_SECONDARY_CTRL  0x376

#define IDE_SECTOR_SIZE     512
#define IDE_MAX_DRIVES      4

typedef struct {
    uint16_t base;
    uint16_t ctrl;
    uint8_t  slave;     // 0 = master, 1 = slave
    uint32_t sectors;   // LBA28 total sector count
} ide_drive_t;

// Wait until BSY clears. Returns 0 on success, -1 on timeout.
static int ide_wait_ready(uint16_t base) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

// Wait until DRQ is set (data ready). Returns 0 on success, -1 on error/timeout.
static int ide_wait_drq(uint16_t base) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR)
            return -1;
        if (status & ATA_SR_DRQ)
            return 0;
    }
    return -1;
}

// Try to identify a drive. Returns 1 if found, 0 if absent.
static int ide_identify(ide_drive_t* drive, uint16_t identify_buf[256]) {
    uint16_t base = drive->base;

    // Select drive (master=0xA0, slave=0xB0)
    outb(base + ATA_REG_DRIVE, 0xA0 | (drive->slave << 4));

    // Clear registers
    outb(base + ATA_REG_SECCOUNT, 0);
    outb(base + ATA_REG_LBA_LO,  0);
    outb(base + ATA_REG_LBA_MID, 0);
    outb(base + ATA_REG_LBA_HI,  0);

    // Send IDENTIFY
    outb(base + ATA_REG_CMD, ATA_CMD_IDENTIFY);

    // Status=0 means no drive
    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0)
        return 0;

    // Wait for BSY to clear
    if (ide_wait_ready(base) < 0)
        return 0;

    // Non-ATA device (ATAPI etc.) — mid/hi ports will be non-zero
    if (inb(base + ATA_REG_LBA_MID) != 0 || inb(base + ATA_REG_LBA_HI) != 0)
        return 0;

    // Wait for DRQ
    if (ide_wait_drq(base) < 0)
        return 0;

    // Read 256 words of IDENTIFY data
    for (int i = 0; i < 256; i++)
        identify_buf[i] = inw(base + ATA_REG_DATA);

    // Extract LBA28 sector count from words 60-61
    drive->sectors = (uint32_t)identify_buf[ATA_IDENT_LBA28_SECTORS] |
                     ((uint32_t)identify_buf[ATA_IDENT_LBA28_SECTORS + 1] << 16);

    return (drive->sectors > 0) ? 1 : 0;
}

static int ide_read_blocks(struct block_device* dev, uint64_t lba, size_t count, void* buf) {
    ide_drive_t* drive = (ide_drive_t*)dev->private_data;
    uint16_t base = drive->base;
    uint16_t* out = (uint16_t*)buf;

    if (lba + count > drive->sectors)
        return -EINVAL;

    for (size_t i = 0; i < count; i++, lba++) {
        if (ide_wait_ready(base) < 0)
            return -EIO;

        // LBA28 addressing
        outb(base + ATA_REG_DRIVE,    0xE0 | (drive->slave << 4) | ((lba >> 24) & 0x0F));
        outb(base + ATA_REG_SECCOUNT, 1);
        outb(base + ATA_REG_LBA_LO,   (uint8_t)(lba));
        outb(base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
        outb(base + ATA_REG_LBA_HI,   (uint8_t)(lba >> 16));
        outb(base + ATA_REG_CMD,       ATA_CMD_READ_PIO);

        if (ide_wait_drq(base) < 0)
            return -EIO;

        for (int w = 0; w < 256; w++)
            out[w] = inw(base + ATA_REG_DATA);

        out += 256; // advance 512 bytes
    }

    return 0;
}

static int ide_write_blocks(struct block_device* dev, uint64_t lba, size_t count, const void* buf) {
    (void)dev; (void)lba; (void)count; (void)buf;
    return -EROFS;
}

void ide_init(void) {
    static const struct {
        uint16_t base;
        uint16_t ctrl;
        uint8_t  slave;
        const char* name;
    } configs[IDE_MAX_DRIVES] = {
        { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL,   0, "hda" },
        { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL,   1, "hdb" },
        { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 0, "hdc" },
        { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 1, "hdd" },
    };

    uint16_t identify_buf[256];

    for (int i = 0; i < IDE_MAX_DRIVES; i++) {
        ide_drive_t drive = {
            .base   = configs[i].base,
            .ctrl   = configs[i].ctrl,
            .slave  = configs[i].slave,
            .sectors = 0,
        };

        if (!ide_identify(&drive, identify_buf))
            continue;

        ide_drive_t* d = kmalloc(sizeof(ide_drive_t));
        if (!d) {
            LOG_WARN("ide: out of memory for %s\n", configs[i].name);
            continue;
        }
        *d = drive;

        block_device_ops_t ops = {
            .read_blocks  = ide_read_blocks,
            .write_blocks = ide_write_blocks,
        };

        register_block_device(configs[i].name, IDE_SECTOR_SIZE, drive.sectors, &ops, d);
        LOG_INFO("ide: %s: %u sectors (%u MiB)\n",
                 configs[i].name, drive.sectors,
                 (uint32_t)(drive.sectors / 2048));
    }
}
