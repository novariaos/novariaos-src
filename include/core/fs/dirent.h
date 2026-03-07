#ifndef DIRENT_H
#define DIRENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Generic directory entry helpers for filesystem implementations
// Provides utilities for parsing and managing directory entries

#define MAX_DIRENT_NAME 255

// Generic directory entry structure
typedef struct fs_dirent {
    uint32_t ino;              // Inode number
    uint8_t type;              // File type (DT_REG, DT_DIR, etc.)
    uint16_t name_len;         // Name length
    char name[MAX_DIRENT_NAME + 1];  // Null-terminated name
} fs_dirent_t;

// Directory entry types (compatible with POSIX d_type)
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12

// Directory entry utilities
int dirent_init(fs_dirent_t* dent, uint32_t ino, uint8_t type, const char* name);
int dirent_name_valid(const char* name, size_t len);
int dirent_compare(const fs_dirent_t* a, const fs_dirent_t* b);

// Directory entry parsing helpers (for filesystem-specific formats)
// These help convert between filesystem-specific directory formats and generic fs_dirent_t

// Parse null-terminated name from buffer, returns length or -1 on error
int dirent_parse_name(const char* buf, size_t max_len, char* name_out, size_t name_out_size);

// Calculate aligned directory entry size (for block-aligned filesystems)
size_t dirent_aligned_size(size_t name_len, size_t alignment);

#endif // DIRENT_H
