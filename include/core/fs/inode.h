#ifndef INODE_H
#define INODE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Generic inode cache for filesystem implementations
// Provides common inode management functionality

#define MAX_CACHED_INODES 128

// Generic inode structure (filesystem-specific data stored separately)
typedef struct inode {
    uint32_t ino;              // Inode number
    uint32_t mode;             // File mode (type + permissions)
    uint32_t uid;              // Owner user ID
    uint32_t gid;              // Owner group ID
    uint64_t size;             // File size in bytes
    uint64_t atime;            // Last access time
    uint64_t mtime;            // Last modification time
    uint64_t ctime;            // Creation time
    uint32_t nlink;            // Number of hard links
    uint32_t blocks;           // Number of blocks allocated

    void* fs_private;          // Filesystem-specific data
    bool dirty;                // Inode needs to be written back
    bool used;                 // Slot is in use
    uint32_t ref_count;        // Reference counter
} inode_t;

// Inode cache operations
void inode_cache_init(void);
inode_t* inode_cache_get(uint32_t ino);
inode_t* inode_cache_alloc(uint32_t ino);
void inode_cache_put(inode_t* inode);
void inode_cache_sync(void);
int inode_cache_evict(uint32_t ino);

// Inode type checks (using standard POSIX mode bits)
#define INODE_IFMT   0170000  // File type mask
#define INODE_IFSOCK 0140000  // Socket
#define INODE_IFLNK  0120000  // Symbolic link
#define INODE_IFREG  0100000  // Regular file
#define INODE_IFBLK  0060000  // Block device
#define INODE_IFDIR  0040000  // Directory
#define INODE_IFCHR  0020000  // Character device
#define INODE_IFIFO  0010000  // FIFO

#define INODE_ISREG(m)  (((m) & INODE_IFMT) == INODE_IFREG)
#define INODE_ISDIR(m)  (((m) & INODE_IFMT) == INODE_IFDIR)
#define INODE_ISLNK(m)  (((m) & INODE_IFMT) == INODE_IFLNK)
#define INODE_ISBLK(m)  (((m) & INODE_IFMT) == INODE_IFBLK)
#define INODE_ISCHR(m)  (((m) & INODE_IFMT) == INODE_IFCHR)

#endif // INODE_H
