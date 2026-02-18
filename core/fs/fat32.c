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

// --- Cluster chain management ---

int fat32_read_fat_entry(fat32_fs_t* fs, uint32_t cluster, uint32_t* out_entry) {
    if (!fs || !out_entry) return -EINVAL;
    if (cluster < 2 || cluster >= fs->total_clusters + 2) {
        LOG_ERROR("fat32_read_fat_entry: cluster %u out of range\n", cluster);
        return -EINVAL;
    }

    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->reserved_sectors + (fat_offset / fs->bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % fs->bytes_per_sector;

    uint8_t* sector_buf = kmalloc(fs->bytes_per_sector);
    if (!sector_buf) return -ENOMEM;

    int rc = fs->block_dev->ops.read_blocks(fs->block_dev, fat_sector, 1, sector_buf);
    if (rc != 0) {
        LOG_ERROR("fat32_read_fat_entry: read failed at sector %u: %d\n", fat_sector, rc);
        kfree(sector_buf);
        return rc;
    }

    uint32_t raw = le32_to_cpu(*(uint32_t*)(sector_buf + offset_in_sector));
    *out_entry = raw & FAT32_MASK;

    kfree(sector_buf);
    return 0;
}

int fat32_write_fat_entry(fat32_fs_t* fs, uint32_t cluster, uint32_t value) {
    if (!fs) return -EINVAL;
    if (cluster < 2 || cluster >= fs->total_clusters + 2) {
        LOG_ERROR("fat32_write_fat_entry: cluster %u out of range\n", cluster);
        return -EINVAL;
    }

    uint32_t fat_offset = cluster * 4;
    uint32_t sector_offset_in_fat = fat_offset / fs->bytes_per_sector;
    uint32_t offset_in_sector = fat_offset % fs->bytes_per_sector;

    uint8_t* sector_buf = kmalloc(fs->bytes_per_sector);
    if (!sector_buf) return -ENOMEM;

    for (uint32_t i = 0; i < fs->num_fats; i++) {
        uint32_t fat_sector = fs->reserved_sectors +
                              (i * fs->fat_size) + sector_offset_in_fat;

        int rc = fs->block_dev->ops.read_blocks(fs->block_dev, fat_sector, 1, sector_buf);
        if (rc != 0) {
            LOG_ERROR("fat32_write_fat_entry: read failed at sector %u: %d\n", fat_sector, rc);
            kfree(sector_buf);
            return rc;
        }

        uint32_t* entry_ptr = (uint32_t*)(sector_buf + offset_in_sector);
        uint32_t old_raw = le32_to_cpu(*entry_ptr);
        uint32_t new_raw = (old_raw & ~FAT32_MASK) | (value & FAT32_MASK);
        *entry_ptr = cpu_to_le32(new_raw);

        rc = fs->block_dev->ops.write_blocks(fs->block_dev, fat_sector, 1, sector_buf);
        if (rc != 0) {
            LOG_ERROR("fat32_write_fat_entry: write failed at sector %u: %d\n", fat_sector, rc);
            kfree(sector_buf);
            return rc;
        }
    }

    kfree(sector_buf);
    return 0;
}

int fat32_get_cluster_chain(fat32_fs_t* fs, uint32_t start_cluster,
                            uint32_t* chain, size_t max_len, size_t* out_len) {
    if (!fs || !chain || !out_len || max_len == 0) return -EINVAL;

    size_t count = 0;
    uint32_t cluster = start_cluster;

    while (count < max_len) {
        if (cluster < 2 || fat32_is_bad(cluster) || fat32_is_eoc(cluster))
            break;

        chain[count++] = cluster;

        uint32_t next;
        int rc = fat32_read_fat_entry(fs, cluster, &next);
        if (rc != 0) return rc;

        cluster = next;
    }

    *out_len = count;
    return 0;
}

int fat32_alloc_cluster(fat32_fs_t* fs, uint32_t* out_cluster) {
    if (!fs || !out_cluster) return -EINVAL;

    for (uint32_t c = 2; c < fs->total_clusters + 2; c++) {
        uint32_t entry;
        int rc = fat32_read_fat_entry(fs, c, &entry);
        if (rc != 0) return rc;

        if (fat32_is_free(entry)) {
            rc = fat32_write_fat_entry(fs, c, FAT32_EOC);
            if (rc != 0) return rc;

            *out_cluster = c;
            LOG_DEBUG("fat32_alloc_cluster: allocated cluster %u\n", c);
            return 0;
        }
    }

    LOG_ERROR("fat32_alloc_cluster: no free clusters\n");
    return -ENOSPC;
}

int fat32_extend_chain(fat32_fs_t* fs, uint32_t last_cluster, uint32_t* out_new) {
    if (!fs || !out_new) return -EINVAL;

    uint32_t new_cluster;
    int rc = fat32_alloc_cluster(fs, &new_cluster);
    if (rc != 0) return rc;

    rc = fat32_write_fat_entry(fs, last_cluster, new_cluster);
    if (rc != 0) {
        fat32_write_fat_entry(fs, new_cluster, FAT32_FREE);
        return rc;
    }

    *out_new = new_cluster;
    return 0;
}

int fat32_free_chain(fat32_fs_t* fs, uint32_t start_cluster) {
    if (!fs) return -EINVAL;

    uint32_t cluster = start_cluster;

    while (cluster >= 2 && !fat32_is_free(cluster) && !fat32_is_bad(cluster)) {
        uint32_t next;
        int rc = fat32_read_fat_entry(fs, cluster, &next);
        if (rc != 0) return rc;

        rc = fat32_write_fat_entry(fs, cluster, FAT32_FREE);
        if (rc != 0) return rc;

        LOG_TRACE("fat32_free_chain: freed cluster %u\n", cluster);

        if (fat32_is_eoc(next))
            break;

        cluster = next;
    }

    return 0;
}

uint32_t fat32_cluster_to_sector(fat32_fs_t* fs, uint32_t cluster) {
    return fs->data_start_sector + (uint32_t)(cluster - 2) * fs->sectors_per_cluster;
}

int fat32_read_cluster(fat32_fs_t* fs, uint32_t cluster, void* buffer) {
    if (!fs || !buffer) return -EINVAL;
    if (cluster < 2 || cluster >= fs->total_clusters + 2) return -EINVAL;

    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return fs->block_dev->ops.read_blocks(fs->block_dev, sector,
                                          fs->sectors_per_cluster, buffer);
}

int fat32_write_cluster(fat32_fs_t* fs, uint32_t cluster, const void* buffer) {
    if (!fs || !buffer) return -EINVAL;
    if (cluster < 2 || cluster >= fs->total_clusters + 2) return -EINVAL;

    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return fs->block_dev->ops.write_blocks(fs->block_dev, sector,
                                           fs->sectors_per_cluster, buffer);
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
