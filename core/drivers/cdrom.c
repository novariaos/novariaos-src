// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/cdrom.h>
#include <core/kernel/kstd.h>
#include <core/fs/block.h>
#include <string.h>

#define CDROM_BLOCK_SIZE 2048

static void* iso_memory = NULL;
static size_t iso_size = 0;

static int cdrom_read_blocks_impl(struct block_device* dev, uint64_t lba, size_t count, void* buf) {
    if (!iso_memory || !dev) {
        return -ENODEV;
    }

    uint64_t offset = lba * CDROM_BLOCK_SIZE;
    size_t bytes_to_read = count * CDROM_BLOCK_SIZE;

    if (offset + bytes_to_read > iso_size) {
        return -EINVAL; // Reading past end of device
    }

    memcpy(buf, (int8_t*)iso_memory + offset, bytes_to_read);
    return 0; // Success
}

static int cdrom_write_blocks_impl(struct block_device* dev, uint64_t lba, size_t count, const void* buf) {
    (void)dev; (void)lba; (void)count; (void)buf;
    return -EROFS; // Read-only filesystem
}

bool cdrom_init(void) {
    // The ISO data must be set via cdrom_set_iso_data before this works.
    // This happens during kernel initialization.
    if (iso_memory && iso_size > 0) {
        block_device_ops_t ops = {
            .read_blocks = cdrom_read_blocks_impl,
            .write_blocks = cdrom_write_blocks_impl,
        };
        uint64_t total_blocks = iso_size / CDROM_BLOCK_SIZE;
        register_block_device("cdrom0", CDROM_BLOCK_SIZE, total_blocks, &ops, NULL);
    }
    return true;
}

void cdrom_set_iso_data(void* data, size_t size) {
    iso_memory = data;
    iso_size = size;
}
