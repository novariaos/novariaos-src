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

// --- Directory entry structures ---

// FAT32 8.3 directory entry (32 bytes)
typedef struct {
    char     name[8];           // Short filename (space-padded)
    char     ext[3];            // Extension (space-padded)
    uint8_t  attr;              // File attributes
    uint8_t  nt_reserved;       // Reserved for Windows NT
    uint8_t  crt_time_tenth;    // Creation time, tenths of a second
    uint16_t crt_time;          // Creation time
    uint16_t crt_date;          // Creation date
    uint16_t acc_date;          // Last access date
    uint16_t fst_clus_hi;       // High 16 bits of first cluster
    uint16_t wrt_time;          // Last write time
    uint16_t wrt_date;          // Last write date
    uint16_t fst_clus_lo;       // Low 16 bits of first cluster
    uint32_t file_size;         // File size in bytes
} __attribute__((packed)) fat32_dir_entry_t;

// FAT32 Long File Name entry (32 bytes, same size as dir entry)
typedef struct {
    uint8_t  order;             // Sequence number (ORed with 0x40 for last entry)
    uint16_t name1[5];          // First 5 UTF-16LE characters
    uint8_t  attr;              // Always ATTR_LONG_NAME (0x0F)
    uint8_t  type;              // Always 0
    uint8_t  checksum;          // Checksum of the 8.3 name
    uint16_t name2[6];          // Next 6 UTF-16LE characters
    uint16_t fst_clus_lo;       // Always 0
    uint16_t name3[2];          // Last 2 UTF-16LE characters
} __attribute__((packed)) fat32_lfn_entry_t;

// File attribute bits
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | \
                             FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)
#define FAT_ATTR_LONG_NAME_MASK (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | \
                                  FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID | \
                                  FAT_ATTR_DIRECTORY | FAT_ATTR_ARCHIVE)

// Special first-byte values for directory entries
#define FAT_ENTRY_FREE      0xE5    // Entry is free (deleted)
#define FAT_ENTRY_END       0x00    // No more entries follow
#define FAT_ENTRY_DOT       0x2E    // '.' entry

// LFN sequence number flags
#define FAT_LFN_LAST        0x40    // This is the last (highest order) LFN entry
#define FAT_LFN_SEQ_MASK    0x3F    // Mask for sequence number

// Max characters in LFN (13 per entry, up to 20 entries = 255 chars + NUL)
#define FAT_LFN_MAX         256

// Parsed directory entry (internal representation)
typedef struct {
    char     name[FAT_LFN_MAX]; // Full filename (LFN if available, else 8.3)
    uint32_t first_cluster;     // First cluster of the file/directory
    uint32_t file_size;         // Size in bytes (0 for directories)
    uint8_t  attr;              // Raw FAT attribute byte
    bool     is_dir;            // True if this is a directory
} fat32_entry_t;

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

// --- Directory entry reading ---
//
// Read all directory entries from a directory starting at @dir_cluster.
// Handles both 8.3 short names and Long File Names (LFN). LFN sequences
// are automatically reconstructed and validated via checksum.
//
// @fs:          FAT32 filesystem instance
// @dir_cluster: First cluster of the directory to read
// @entries:     Output buffer for parsed entries
// @max_entries: Maximum number of entries to read
// @out_count:   Number of entries actually read
//
// Returns: 0 on success, negative error code otherwise
//
// Note: Includes protection against infinite loops on corrupted FAT chains.
int fat32_read_dir(fat32_fs_t* fs, uint32_t dir_cluster,
                   fat32_entry_t* entries, size_t max_entries, size_t* out_count);

// Lookup a directory entry by name within a given directory cluster.
// Comparison is case-insensitive for ASCII characters (FAT32 convention).
//
// @fs:          FAT32 filesystem instance
// @dir_cluster: First cluster of the directory to search
// @name:        Entry name to find (supports both 8.3 and LFN)
// @out:         Output structure filled with entry details on success
//
// Returns: 0 and fills *out on success, -ENOENT if not found
int fat32_lookup(fat32_fs_t* fs, uint32_t dir_cluster,
                 const char* name, fat32_entry_t* out);

// --- VFS interface ---
//
// These functions provide the glue between FAT32 file/directory operations
// and the Virtual File System (VFS) layer.

// Open a file and populate the file handle. Allocates private_data (fat32_entry_t).
// Returns 0 on success, negative error code otherwise.
int fat32_vfs_open(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h);

// Close a file handle and free associated private_data.
// Returns 0 on success, negative error code otherwise.
int fat32_vfs_close(vfs_mount_t* mnt, vfs_file_handle_t* h);

// Read up to @count bytes from the file at h->position into @buf.
// Advances h->position by the number of bytes actually read.
// Returns number of bytes read (0 = EOF), or negative error code.
vfs_ssize_t fat32_vfs_read(vfs_mount_t* mnt, vfs_file_handle_t* h,
                            void* buf, size_t count);

// Read directory contents through VFS interface.
// Returns the number of entries read, or negative error code.
int fat32_vfs_readdir(vfs_mount_t* mnt, const char* path,
                      vfs_dirent_t* entries, size_t max_entries);

// Get file/directory metadata (size, type, timestamps).
// Returns 0 on success, negative error code otherwise.
int fat32_vfs_stat(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat);

#endif
