// SPDX-License-Identifier: GPL-3.0-only

#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <core/fs/block.h>
#include <core/fs/vfs.h>

typedef struct {
    uint8_t  jump_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
    uint8_t  boot_code[420];
    uint16_t signature;
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    block_device_t* block_dev;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint32_t data_start_sector;
    uint32_t total_clusters;
} fat32_fs_t;

// FAT32 special cluster values
#define FAT32_MASK      0x0FFFFFFF
#define FAT32_FREE      0x00000000
#define FAT32_BAD       0x0FFFFFF7
#define FAT32_EOC_MIN   0x0FFFFFF8
#define FAT32_EOC       0x0FFFFFFF

static inline bool fat32_is_eoc(uint32_t entry) {
    return (entry & FAT32_MASK) >= FAT32_EOC_MIN;
}

static inline bool fat32_is_free(uint32_t entry) {
    return (entry & FAT32_MASK) == FAT32_FREE;
}

static inline bool fat32_is_bad(uint32_t entry) {
    return (entry & FAT32_MASK) == FAT32_BAD;
}

// Initialization and mount
void fat32_init(void);
int fat32_mount(vfs_mount_t* mnt, const char* device, void* data);
int fat32_unmount(vfs_mount_t* mnt);

// FAT table operations
int fat32_read_fat_entry(fat32_fs_t* fs, uint32_t cluster, uint32_t* out_entry);
int fat32_write_fat_entry(fat32_fs_t* fs, uint32_t cluster, uint32_t value);

// Cluster chain traversal
int fat32_get_cluster_chain(fat32_fs_t* fs, uint32_t start_cluster,
                            uint32_t* chain, size_t max_len, size_t* out_len);

// Cluster allocation and deallocation
int fat32_alloc_cluster(fat32_fs_t* fs, uint32_t* out_cluster);
int fat32_extend_chain(fat32_fs_t* fs, uint32_t last_cluster, uint32_t* out_new);
int fat32_free_chain(fat32_fs_t* fs, uint32_t start_cluster);

// Cluster I/O
uint32_t fat32_cluster_to_sector(fat32_fs_t* fs, uint32_t cluster);
int fat32_read_cluster(fat32_fs_t* fs, uint32_t cluster, void* buffer);
int fat32_write_cluster(fat32_fs_t* fs, uint32_t cluster, const void* buffer);
#endif
