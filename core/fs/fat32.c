// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/fat32.h>
#include <core/fs/block.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>
#include <core/kernel/mem.h>

static const vfs_fs_ops_t fat32_ops;

void fat32_init(void) {
    vfs_register_filesystem("fat32", &fat32_ops, 0);
    LOG_INFO("FAT32 filesystem driver registered\n");
}

int fat32_mount(vfs_mount_t* mnt, const char* device, void* data) {
    LOG_DEBUG("Mounting FAT32 filesystem on device: %s\n", device);
    
    block_device_t* bdev = find_block_device(device);
    if (!bdev) {
        LOG_ERROR("Block device '%s' not found\n", device);
        return -19;
    }
    
    uint8_t* boot_sector = kmalloc(bdev->block_size);
    if (!boot_sector) {
        return -12;
    }
    
    int result = bdev->ops.read_blocks(bdev, 0, 1, boot_sector);
    if (result != 0) {
        LOG_ERROR("Failed to read boot sector: %d\n", result);
        kfree(boot_sector);
        return result;
    }
    
    fat32_bpb_t* bpb = (fat32_bpb_t*)boot_sector;
    
    uint16_t signature = le16_to_cpu(bpb->signature);
    if (signature != 0xAA55) {
        LOG_ERROR("Invalid boot signature: 0x%X (expected 0xAA55)\n", signature);
        kfree(boot_sector);
        return -22;
    }
    
    if (strncmp(bpb->fs_type, "FAT32   ", 8) != 0) {
        LOG_WARN("Filesystem type is not 'FAT32': %.8s\n", bpb->fs_type);
    }
    
    fat32_fs_t* fs_data = kmalloc(sizeof(fat32_fs_t));
    if (!fs_data) {
        kfree(boot_sector);
        return -12;
    }
    
    fs_data->block_dev = bdev;
    fs_data->bytes_per_sector = le16_to_cpu(bpb->bytes_per_sector);
    fs_data->sectors_per_cluster = bpb->sectors_per_cluster;
    fs_data->bytes_per_cluster = fs_data->bytes_per_sector * fs_data->sectors_per_cluster;
    fs_data->reserved_sectors = le16_to_cpu(bpb->reserved_sectors);
    fs_data->num_fats = bpb->num_fats;
    fs_data->fat_size = le32_to_cpu(bpb->fat_size_32);
    fs_data->root_cluster = le32_to_cpu(bpb->root_cluster);
    
    uint32_t total_sectors_16 = le16_to_cpu(bpb->total_sectors_16);
    fs_data->total_sectors = (total_sectors_16 != 0) ? 
        total_sectors_16 : le32_to_cpu(bpb->total_sectors_32);
    
    fs_data->data_start_sector = fs_data->reserved_sectors + 
        (fs_data->num_fats * fs_data->fat_size);
    
    uint32_t data_sectors = fs_data->total_sectors - fs_data->data_start_sector;
    fs_data->total_clusters = data_sectors / fs_data->sectors_per_cluster;
    
    if (fs_data->total_clusters < 65525) {
        LOG_ERROR("Too few clusters for FAT32: %u (need >= 65525)\n", 
                  fs_data->total_clusters);
        kfree(fs_data);
        kfree(boot_sector);
        return -22;
    }
    
    LOG_INFO("FAT32 mounted successfully:\n");
    LOG_INFO("  Volume Label: %.11s\n", bpb->volume_label);
    LOG_INFO("  Bytes/Sector: %u\n", fs_data->bytes_per_sector);
    LOG_INFO("  Sectors/Cluster: %u\n", fs_data->sectors_per_cluster);
    LOG_INFO("  Reserved Sectors: %u\n", fs_data->reserved_sectors);
    LOG_INFO("  Number of FATs: %u\n", fs_data->num_fats);
    LOG_INFO("  FAT Size: %u sectors\n", fs_data->fat_size);
    LOG_INFO("  Root Cluster: %u\n", fs_data->root_cluster);
    LOG_INFO("  Total Sectors: %u\n", fs_data->total_sectors);
    LOG_INFO("  Total Clusters: %u\n", fs_data->total_clusters);
    
    mnt->fs_private = fs_data;
    
    kfree(boot_sector);
    return 0;
}

int fat32_unmount(vfs_mount_t* mnt) {
    if (!mnt || !mnt->fs_private) {
        return -22;
    }
    
    fat32_fs_t* fs_data = (fat32_fs_t*)mnt->fs_private;
    kfree(fs_data);
    mnt->fs_private = NULL;
    
    LOG_INFO("FAT32 filesystem unmounted\n");
    return 0;
}

static const vfs_fs_ops_t fat32_ops = {
    .name = "fat32",
    .mount = fat32_mount,
    .unmount = fat32_unmount,
    .open = NULL,
    .close = NULL,
    .read = NULL,
    .write = NULL,
    .seek = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .readdir = NULL,
    .stat = NULL,
    .unlink = NULL,
    .ioctl = NULL,
    .sync = NULL,
};
