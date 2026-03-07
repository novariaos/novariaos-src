// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/inode.h>
#include <core/kernel/kstd.h>

static inode_t inode_cache[MAX_CACHED_INODES];

void inode_cache_init(void) {
    for (int i = 0; i < MAX_CACHED_INODES; i++) {
        inode_cache[i].used = false;
        inode_cache[i].dirty = false;
        inode_cache[i].ref_count = 0;
        inode_cache[i].fs_private = NULL;
    }
}

inode_t* inode_cache_get(uint32_t ino) {
    for (int i = 0; i < MAX_CACHED_INODES; i++) {
        if (inode_cache[i].used && inode_cache[i].ino == ino) {
            inode_cache[i].ref_count++;
            return &inode_cache[i];
        }
    }
    return NULL;
}

inode_t* inode_cache_alloc(uint32_t ino) {
    // First check if already cached
    inode_t* existing = inode_cache_get(ino);
    if (existing) {
        return existing;
    }

    // Find free slot
    for (int i = 0; i < MAX_CACHED_INODES; i++) {
        if (!inode_cache[i].used) {
            memset(&inode_cache[i], 0, sizeof(inode_t));
            inode_cache[i].ino = ino;
            inode_cache[i].used = true;
            inode_cache[i].ref_count = 1;
            inode_cache[i].dirty = false;
            return &inode_cache[i];
        }
    }

    // Cache full - evict least recently used (simple: find ref_count == 0)
    for (int i = 0; i < MAX_CACHED_INODES; i++) {
        if (inode_cache[i].ref_count == 0) {
            memset(&inode_cache[i], 0, sizeof(inode_t));
            inode_cache[i].ino = ino;
            inode_cache[i].used = true;
            inode_cache[i].ref_count = 1;
            inode_cache[i].dirty = false;
            return &inode_cache[i];
        }
    }

    return NULL; // Cache full, all inodes referenced
}

void inode_cache_put(inode_t* inode) {
    if (inode && inode->ref_count > 0) {
        inode->ref_count--;
    }
}

void inode_cache_sync(void) {
    // Mark all dirty inodes for writeback (filesystem-specific sync handles actual write)
    for (int i = 0; i < MAX_CACHED_INODES; i++) {
        if (inode_cache[i].used && inode_cache[i].dirty) {
            // Filesystem driver should implement actual writeback
            inode_cache[i].dirty = false;
        }
    }
}

int inode_cache_evict(uint32_t ino) {
    for (int i = 0; i < MAX_CACHED_INODES; i++) {
        if (inode_cache[i].used && inode_cache[i].ino == ino) {
            if (inode_cache[i].ref_count > 0) {
                return -1; // Still referenced, cannot evict
            }
            inode_cache[i].used = false;
            inode_cache[i].fs_private = NULL;
            return 0;
        }
    }
    return -1; // Not found
}
