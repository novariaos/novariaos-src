// SPDX-License-Identifier: GPL-3.0-only

#ifndef CORE_FS_FAT32_H
#define CORE_FS_FAT32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <core/fs/vfs.h>
#include <core/fs/block.h>

// FAT32 signatures
#define FAT32_SIGNATURE 0x28
#define FAT32_SIGNATURE2 0x29

// FAT32 constants
#define FAT32_FREE      0x00000000
#define FAT32_RESERVED  0x0FFFFFF0
#define FAT32_BAD       0x0FFFFFF7
#define FAT32_EOC       0x0FFFFFF8  // End of clusterchain (or >= this)

// FAT32 masks
#define FAT32_MASK      0x0FFFFFFF

// Directory entry attributes
#define FAT_ATTR_READ_ONLY      0x01
#define FAT_ATTR_HIDDEN         0x02
#define FAT_ATTR_SYSTEM         0x04
#define FAT_ATTR_VOLUME_ID      0x08
#define FAT_ATTR_DIRECTORY      0x10
#define FAT_ATTR_ARCHIVE        0x20
#define FAT_ATTR_LONG_NAME      (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | \
                                 FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)
#define FAT_ATTR_LONG_NAME_MASK 0x3F

// LFN constants
#define FAT_LFN_LAST            0x40
#define FAT_LFN_SEQ_MASK        0x1F
#define FAT_LFN_MAX             256

// Directory entry markers
#define FAT_ENTRY_FREE          0xE5
#define FAT_ENTRY_END           0x00

// Helper macros for FAT cluster status
#define fat32_is_free(x)  ((x) == FAT32_FREE)
#define fat32_is_reserved(x) (((x) & 0x0FFFFFF8) == FAT32_RESERVED)
#define fat32_is_bad(x)   ((x) == FAT32_BAD)
#define fat32_is_eoc(x)   ((x) >= FAT32_EOC)

// FAT32 BPB structure (BIOS Parameter Block)
typedef struct __attribute__((packed)) {
    uint8_t jmp_boot[3];
    char oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_descriptor;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t mirror_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
    uint8_t boot_code[420];
    uint16_t signature;  // 0xAA55
} fat32_bpb_t;

// FAT32 directory entry (8.3 format)
typedef struct __attribute__((packed)) {
    char name[8];
    char ext[3];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t fst_clus_hi;  // high 16 bits of first cluster
    uint16_t write_time;
    uint16_t write_date;
    uint16_t fst_clus_lo;  // low 16 bits of first cluster
    uint32_t file_size;
} fat32_dir_entry_t;

// FAT32 LFN entry (Long File Name)
typedef struct __attribute__((packed)) {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t reserved;
    uint16_t name3[2];
} fat32_lfn_entry_t;

// FS-level data structure (per mount)
typedef struct fat32_fs {
    block_device_t* block_dev;
    
    // Cached BPB values
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t total_sectors;
    
    // Derived values
    uint32_t data_start_sector;
    uint32_t total_clusters;
} fat32_fs_t;

// In-memory directory entry (parsed, with full name)
typedef struct fat32_entry {
    char name[FAT_LFN_MAX];
    uint32_t first_cluster;
    uint32_t file_size;
    uint8_t attr;
    bool is_dir;
} fat32_entry_t;

// File handle for open files
typedef struct fat32_file_handle {
    uint32_t first_cluster;
    uint32_t file_size;
    uint32_t dir_cluster;          // cluster of parent directory
    uint32_t dir_entry_cluster;     // cluster containing the dir entry
    uint32_t dir_entry_index;       // index within that cluster
} fat32_file_handle_t;

// Public API
void fat32_init(void);
bool fat32_is_initialized(void);
int fat32_mount_to_vfs(const char* mount_point, const char* device);

// VFS operations (declared for internal use, but called via ops table)
int fat32_mount(vfs_mount_t* mnt, const char* device, void* data);
int fat32_unmount(vfs_mount_t* mnt);
int fat32_vfs_open(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h);
int fat32_vfs_close(vfs_mount_t* mnt, vfs_file_handle_t* h);
vfs_ssize_t fat32_vfs_read(vfs_mount_t* mnt, vfs_file_handle_t* h, void* buf, size_t count);
vfs_ssize_t fat32_vfs_write(vfs_mount_t* mnt, vfs_file_handle_t* h, const void* buf, size_t count);
vfs_off_t fat32_vfs_seek(vfs_mount_t* mnt, vfs_file_handle_t* h, vfs_off_t offset, int whence);
int fat32_vfs_readdir(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max_entries);
int fat32_vfs_stat(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat);

// Internal functions (used across the driver)
int fat32_read_fat_entry(fat32_fs_t* fs, uint32_t cluster, uint32_t* out_entry);
int fat32_write_fat_entry(fat32_fs_t* fs, uint32_t cluster, uint32_t value);
int fat32_alloc_cluster(fat32_fs_t* fs, uint32_t* out_cluster);
int fat32_extend_chain(fat32_fs_t* fs, uint32_t last_cluster, uint32_t* out_new);
int fat32_free_chain(fat32_fs_t* fs, uint32_t start_cluster);
int fat32_read_cluster(fat32_fs_t* fs, uint32_t cluster, void* buffer);
int fat32_write_cluster(fat32_fs_t* fs, uint32_t cluster, const void* buffer);
uint32_t fat32_cluster_to_sector(fat32_fs_t* fs, uint32_t cluster);

#endif // CORE_FS_FAT32_H