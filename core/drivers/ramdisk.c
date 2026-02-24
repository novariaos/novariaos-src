// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/ramdisk.h>
#include <core/fs/block.h>
#include <core/kernel/mem/allocator.h>
#include <string.h>

#define RAMDISK_BLOCK_SIZE 512

typedef struct {
    void*  image;
    size_t size;
} ramdisk_t;

static int ramdisk_read_blocks(struct block_device* dev, uint64_t lba, size_t count, void* buf) {
    ramdisk_t* rd = (ramdisk_t*)dev->private_data;
    uint64_t offset = lba * RAMDISK_BLOCK_SIZE;
    size_t   bytes  = count * RAMDISK_BLOCK_SIZE;
    if (offset + bytes > rd->size) return -EINVAL;
    memcpy(buf, (uint8_t*)rd->image + offset, bytes);
    return 0;
}

static int ramdisk_write_blocks(struct block_device* dev, uint64_t lba, size_t count, const void* buf) {
    (void)dev; (void)lba; (void)count; (void)buf;
    return -EROFS;
}

void ramdisk_register(const char* name, void* image, size_t size) {
    ramdisk_t* rd = kmalloc(sizeof(ramdisk_t));
    if (!rd) return;
    rd->image = image;
    rd->size  = size;

    block_device_ops_t ops = {
        .read_blocks  = ramdisk_read_blocks,
        .write_blocks = ramdisk_write_blocks,
    };
    register_block_device(name, RAMDISK_BLOCK_SIZE, size / RAMDISK_BLOCK_SIZE, &ops, rd);
}
