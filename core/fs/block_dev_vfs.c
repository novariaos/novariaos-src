// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/block_dev_vfs.h>
#include <core/fs/block.h>
#include <core/fs/vfs.h>
#include <core/kernel/mem.h>
#include <log.h>
#include <core/kernel/kstd.h>
#include <string.h>

// This is the "glue" layer between the VFS and the Block Device layer.
// It creates VFS-compatible device nodes for each registered block device.

static vfs_ssize_t bdev_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    if (!file || !file->dev_data || !buf) {
        return -EINVAL;
    }

    block_device_t* dev = (block_device_t*)file->dev_data;
    vfs_off_t current_pos = *pos;
    size_t total_bytes_read = 0;
    uint8_t* out_buf = (uint8_t*)buf;

    if (current_pos >= (vfs_off_t)(dev->total_blocks * dev->block_size)) {
        return 0; // EOF
    }

    while (count > 0) {
        uint64_t lba = current_pos / dev->block_size;
        uint64_t offset_in_block = current_pos % dev->block_size;

        // Check for EOF
        if (lba >= dev->total_blocks) {
            break;
        }

        uint8_t* block_buf = kmalloc(dev->block_size);
        if (!block_buf) {
            return -ENOMEM;
        }

        int result = dev->ops.read_blocks(dev, lba, 1, block_buf);
        if (result != 0) {
            kfree(block_buf);
            if (total_bytes_read > 0) {
                break; // Return bytes read so far
            }
            return result;
        }

        size_t bytes_to_copy = dev->block_size - offset_in_block;
        if (bytes_to_copy > count) {
            bytes_to_copy = count;
        }
        
        // Final check to not read past the end of the device
        if (current_pos + bytes_to_copy > dev->total_blocks * dev->block_size) {
            bytes_to_copy = (dev->total_blocks * dev->block_size) - current_pos;
        }

        memcpy(out_buf, block_buf + offset_in_block, bytes_to_copy);

        kfree(block_buf);

        total_bytes_read += bytes_to_copy;
        current_pos += bytes_to_copy;
        out_buf += bytes_to_copy;
        count -= bytes_to_copy;
        
        if (bytes_to_copy == 0 && count > 0) {
            // Should not happen if logic is correct, but as a safeguard
            break;
        }
    }

    *pos = current_pos;
    return total_bytes_read;
}


static vfs_ssize_t bdev_write(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos) {
    if (!file || !file->dev_data) {
        return -EINVAL;
    }
    block_device_t* dev = (block_device_t*)file->dev_data;

    // For simplicity, we just pass this to the driver. 
    // The cdrom driver will return -EROFS. A real disk driver would need translation logic.
    // We pass dummy values for LBA and count as the driver will decide what to do.
    return dev->ops.write_blocks(dev, 0, 0, NULL);
}

void block_dev_vfs_init(void) {
    block_device_t* devices = get_block_devices();
    if (!devices) {
        return;
    }

    LOG_INFO("Registering block devices with VFS...\n");

    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (devices[i].used) {
            char dev_path[MAX_FILENAME];
            strcpy(dev_path, "/dev/");
            strcat(dev_path, devices[i].name);

            vfs_pseudo_register(
                dev_path,
                bdev_read,
                bdev_write,
                NULL, // No seek for now
                NULL, // No ioctl for now
                &devices[i] // Pass a pointer to the block device struct
            );

            LOG_INFO("  Registered %s\n", dev_path);
        }
    }
}