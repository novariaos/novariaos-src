#include <core/drivers/ide.h>
#include <core/fs/block.h>
#include <core/kernel/mem/allocator.h>
#include <log.h>
#include <core/arch/io.h>
#include <stdint.h>
#include <stddef.h>

#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA_LO      3
#define ATA_REG_LBA_MID     4
#define ATA_REG_LBA_HI      5
#define ATA_REG_DRIVE       6
#define ATA_REG_STATUS      7
#define ATA_REG_CMD         7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ_PIO    0x20

#define ATA_IDENT_LBA28_SECTORS 60

#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170
#define ATA_SECONDARY_CTRL  0x376

#define IDE_SECTOR_SIZE     512
#define IDE_MAX_DRIVES      4

typedef struct {
    uint16_t base;
    uint16_t ctrl;
    uint8_t  slave;
    uint32_t sectors;
} ide_drive_t;

static int ide_wait_ready(uint16_t base) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int ide_wait_drq(uint16_t base) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY))
            break;
    }
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR)
            return -1;
        if (status & ATA_SR_DRQ)
            return 0;
    }
    return -1;
}

static int ide_identify(ide_drive_t* drive, uint16_t identify_buf[256]) {
    uint16_t base = drive->base;

    outb(base + ATA_REG_DRIVE, 0xA0 | (drive->slave << 4));

    outb(base + ATA_REG_SECCOUNT, 0);
    outb(base + ATA_REG_LBA_LO,  0);
    outb(base + ATA_REG_LBA_MID, 0);
    outb(base + ATA_REG_LBA_HI,  0);

    outb(base + ATA_REG_CMD, ATA_CMD_IDENTIFY);

    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0)
        return 0;

    if (ide_wait_ready(base) < 0)
        return 0;

    if (inb(base + ATA_REG_LBA_MID) != 0 || inb(base + ATA_REG_LBA_HI) != 0)
        return 0;

    if (ide_wait_drq(base) < 0)
        return 0;

    for (int i = 0; i < 256; i++)
        identify_buf[i] = inw(base + ATA_REG_DATA);

    drive->sectors = (uint32_t)identify_buf[ATA_IDENT_LBA28_SECTORS] |
                     ((uint32_t)identify_buf[ATA_IDENT_LBA28_SECTORS + 1] << 16);

    return (drive->sectors > 0) ? 1 : 0;
}

static inline void ide_io_delay(uint16_t ctrl) {
    inb(ctrl); inb(ctrl); inb(ctrl); inb(ctrl);
}

static int ide_read_blocks(struct block_device* dev, uint64_t lba, size_t count, void* buf) {
    ide_drive_t* drive = (ide_drive_t*)dev->private_data;
    uint16_t base = drive->base;
    uint16_t ctrl = drive->ctrl;
    uint16_t* out = (uint16_t*)buf;

    if (lba + count > drive->sectors)
        return -EINVAL;

    while (count > 0) {
        size_t sectors_this_cmd = (count > 255) ? 255 : count;

        if (ide_wait_ready(base) < 0)
            return -EIO;

        outb(base + ATA_REG_DRIVE,    0xE0 | (drive->slave << 4) | ((lba >> 24) & 0x0F));
        ide_io_delay(ctrl);
        outb(base + ATA_REG_SECCOUNT, (uint8_t)sectors_this_cmd);
        outb(base + ATA_REG_LBA_LO,   (uint8_t)(lba));
        outb(base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
        outb(base + ATA_REG_LBA_HI,   (uint8_t)(lba >> 16));
        outb(base + ATA_REG_CMD,       ATA_CMD_READ_PIO);

        ide_io_delay(ctrl);

        for (size_t s = 0; s < sectors_this_cmd; s++) {
            if (ide_wait_drq(base) < 0)
                return -EIO;

            for (int w = 0; w < 256; w++)
                out[w] = inw(base + ATA_REG_DATA);

            out += 256;
        }

        lba += sectors_this_cmd;
        count -= sectors_this_cmd;
    }

    return 0;
}

#define ATA_CMD_WRITE_PIO 0x30

static int ide_write_blocks(struct block_device* dev, uint64_t lba, size_t count, const void* buf) {
    ide_drive_t* drive = (ide_drive_t*)dev->private_data;
    uint16_t base = drive->base;
    uint16_t ctrl = drive->ctrl;
    const uint16_t* src = (const uint16_t*)buf;

    if (lba + count > drive->sectors)
        return -EINVAL;

    while (count > 0) {
        size_t sectors_this_cmd = (count > 255) ? 255 : count;

        if (ide_wait_ready(base) < 0)
            return -EIO;

        outb(base + ATA_REG_DRIVE,    0xE0 | (drive->slave << 4) | ((lba >> 24) & 0x0F));
        ide_io_delay(ctrl);
        outb(base + ATA_REG_SECCOUNT, (uint8_t)sectors_this_cmd);
        outb(base + ATA_REG_LBA_LO,   (uint8_t)(lba));
        outb(base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
        outb(base + ATA_REG_LBA_HI,   (uint8_t)(lba >> 16));
        outb(base + ATA_REG_CMD,       ATA_CMD_WRITE_PIO);
        ide_io_delay(ctrl);

        for (size_t s = 0; s < sectors_this_cmd; s++) {
            if (ide_wait_drq(base) < 0)
                return -EIO;

            for (int w = 0; w < 256; w++)
                outw(base + ATA_REG_DATA, src[w]);

            src += 256;

            // Wait for drive to finish writing this sector
            if (ide_wait_ready(base) < 0)
                return -EIO;
        }

        // FLUSH CACHE — ensure data hits the platter
        outb(base + ATA_REG_CMD, 0xE7);
        if (ide_wait_ready(base) < 0)
            return -EIO;

        lba += sectors_this_cmd;
        count -= sectors_this_cmd;
    }
    return 0;
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
            continue;
        }
        *d = drive;

        block_device_ops_t ops = {
            .read_blocks  = ide_read_blocks,
            .write_blocks = ide_write_blocks,
        };

        register_block_device(configs[i].name, IDE_SECTOR_SIZE, drive.sectors, &ops, d);
    }
}
