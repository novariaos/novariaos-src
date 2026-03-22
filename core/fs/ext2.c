// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/ext2.h>
#include <core/fs/vfs.h>
#include <core/fs/block.h>
#include <core/kernel/kstd.h>
#include <core/kernel/mem.h>
#include <log.h>
#include <stddef.h>
#include <stdbool.h>

static int ext2_read_block(ext2_fs_t *fs, uint32_t block, void *buf) {
    uint64_t lba = (uint64_t)block * fs->sectors_per_block;
    return fs->block_dev->ops.read_blocks(fs->block_dev, lba,
                                          fs->sectors_per_block, buf);
}

static int ext2_write_block(ext2_fs_t *fs, uint32_t block, const void *buf) {
    uint64_t lba = (uint64_t)block * fs->sectors_per_block;
    return fs->block_dev->ops.write_blocks(fs->block_dev, lba,
                                           fs->sectors_per_block,
                                           (void *)buf);
}

static int ext2_write_superblock(ext2_fs_t *fs) {
    uint8_t *blk = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    uint32_t sb_block = (fs->block_size == 1024) ? 1 : 0;
    if (ext2_read_block(fs, sb_block, blk) != 0) {
        kfree(blk);
        return -EIO;
    }

    uint32_t sb_offset = (fs->block_size == 1024) ? 0 : 1024;
    memcpy(blk + sb_offset, &fs->sb, sizeof(ext2_superblock_t));

    int rc = ext2_write_block(fs, sb_block, blk);
    kfree(blk);
    return rc;
}

static int ext2_write_bgdt(ext2_fs_t *fs) {
    uint32_t bgdt_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t bgdt_size  = fs->num_groups * sizeof(ext2_bgd_t);
    uint32_t bgdt_blocks = (bgdt_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t i = 0; i < bgdt_blocks; i++) {
        uint8_t *blk = kmalloc(fs->block_size);
        if (!blk) return -ENOMEM;
        memset(blk, 0, fs->block_size);
        uint32_t offset  = i * fs->block_size;
        uint32_t to_copy = bgdt_size - offset;
        if (to_copy > fs->block_size) to_copy = fs->block_size;
        memcpy(blk, (uint8_t *)fs->bgdt + offset, to_copy);
        int rc = ext2_write_block(fs, bgdt_block + i, blk);
        kfree(blk);
        if (rc != 0) return rc;
    }
    return 0;
}

static int ext2_read_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *out) {
    if (ino == 0) return -EINVAL;

    uint32_t group  = (ino - 1) / fs->inodes_per_group;
    uint32_t index  = (ino - 1) % fs->inodes_per_group;

    if (group >= fs->num_groups) return -EINVAL;

    uint32_t inode_table_block = fs->bgdt[group].bg_inode_table;
    uint32_t byte_offset       = index * fs->inode_size;
    uint32_t block_in_table    = byte_offset / fs->block_size;
    uint32_t offset_in_block   = byte_offset % fs->block_size;

    uint8_t *blk = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    int rc = ext2_read_block(fs, inode_table_block + block_in_table, blk);
    if (rc == 0)
        memcpy(out, blk + offset_in_block, sizeof(ext2_inode_t));

    kfree(blk);
    return rc;
}

static int ext2_write_inode(ext2_fs_t *fs, uint32_t ino, const ext2_inode_t *in) {
    if (ino == 0) return -EINVAL;

    uint32_t group  = (ino - 1) / fs->inodes_per_group;
    uint32_t index  = (ino - 1) % fs->inodes_per_group;

    if (group >= fs->num_groups) return -EINVAL;

    uint32_t inode_table_block = fs->bgdt[group].bg_inode_table;
    uint32_t byte_offset       = index * fs->inode_size;
    uint32_t block_in_table    = byte_offset / fs->block_size;
    uint32_t offset_in_block   = byte_offset % fs->block_size;

    uint8_t *blk = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    int rc = ext2_read_block(fs, inode_table_block + block_in_table, blk);
    if (rc == 0) {
        memcpy(blk + offset_in_block, in, sizeof(ext2_inode_t));
        rc = ext2_write_block(fs, inode_table_block + block_in_table, blk);
    }
    kfree(blk);
    return rc;
}

#define PTRS_PER_BLOCK(fs) ((fs)->block_size / sizeof(uint32_t))

static uint32_t ext2_read_indirect(ext2_fs_t *fs, uint32_t ind_block, uint32_t index) {
    if (ind_block == 0) return 0;
    uint32_t *table = kmalloc(fs->block_size);
    if (!table) return 0;
    if (ext2_read_block(fs, ind_block, table) != 0) {
        kfree(table);
        return 0;
    }
    uint32_t phys = table[index];
    kfree(table);
    return phys;
}

static uint32_t ext2_bmap(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t lbn) {
    uint32_t ppb = PTRS_PER_BLOCK(fs);

    if (lbn < EXT2_NDIR_BLOCKS) {
        return inode->i_block[lbn];
    }
    lbn -= EXT2_NDIR_BLOCKS;

    if (lbn < ppb) {
        return ext2_read_indirect(fs, inode->i_block[EXT2_IND_BLOCK], lbn);
    }
    lbn -= ppb;

    if (lbn < ppb * ppb) {
        uint32_t l1 = ext2_read_indirect(fs, inode->i_block[EXT2_DIND_BLOCK], lbn / ppb);
        return ext2_read_indirect(fs, l1, lbn % ppb);
    }
    lbn -= ppb * ppb;

    uint32_t l1 = ext2_read_indirect(fs, inode->i_block[EXT2_TIND_BLOCK], lbn / (ppb * ppb));
    uint32_t l2 = ext2_read_indirect(fs, l1, (lbn / ppb) % ppb);
    return ext2_read_indirect(fs, l2, lbn % ppb);
}

static uint32_t ext2_alloc_block_in_group(ext2_fs_t *fs, uint32_t group) {
    if (fs->bgdt[group].bg_free_blocks_count == 0) return 0;

    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) return 0;

    if (ext2_read_block(fs, fs->bgdt[group].bg_block_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return 0;
    }

    uint32_t blocks_in_group = fs->blocks_per_group;
    for (uint32_t i = 0; i < blocks_in_group; i++) {
        uint32_t byte = i / 8;
        uint32_t bit  = i % 8;
        if (!(bitmap[byte] & (1u << bit))) {
            bitmap[byte] |= (1u << bit);
            if (ext2_write_block(fs, fs->bgdt[group].bg_block_bitmap, bitmap) != 0) {
                kfree(bitmap);
                return 0;
            }
            kfree(bitmap);
            uint32_t block = fs->sb.s_first_data_block + group * fs->blocks_per_group + i;
            fs->bgdt[group].bg_free_blocks_count--;
            fs->sb.s_free_blocks_count--;
            return block;
        }
    }
    kfree(bitmap);
    return 0;
}

static uint32_t ext2_alloc_block(ext2_fs_t *fs) {
    for (uint32_t g = 0; g < fs->num_groups; g++) {
        uint32_t b = ext2_alloc_block_in_group(fs, g);
        if (b) return b;
    }
    return 0;
}

static void ext2_free_block(ext2_fs_t *fs, uint32_t block) {
    if (block < fs->sb.s_first_data_block) return;
    uint32_t rel = block - fs->sb.s_first_data_block;
    uint32_t group = rel / fs->blocks_per_group;
    uint32_t index = rel % fs->blocks_per_group;
    if (group >= fs->num_groups) return;

    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) return;
    if (ext2_read_block(fs, fs->bgdt[group].bg_block_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return;
    }
    bitmap[index / 8] &= ~(1u << (index % 8));
    ext2_write_block(fs, fs->bgdt[group].bg_block_bitmap, bitmap);
    kfree(bitmap);
    fs->bgdt[group].bg_free_blocks_count++;
    fs->sb.s_free_blocks_count++;
}

static int ext2_set_indirect(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t *ind_block_ptr,
                             uint32_t index, uint32_t phys_block) {
    uint32_t *table = kmalloc(fs->block_size);
    if (!table) return -ENOMEM;

    uint32_t ind_block = *ind_block_ptr;

    if (ind_block == 0) {
        ind_block = ext2_alloc_block(fs);
        if (ind_block == 0) { kfree(table); return -ENOSPC; }
        memset(table, 0, fs->block_size);
        inode->i_blocks += fs->sectors_per_block;
    } else {
        if (ext2_read_block(fs, ind_block, table) != 0) {
            kfree(table);
            return -EIO;
        }
    }

    table[index] = phys_block;
    int rc = ext2_write_block(fs, ind_block, table);

    if (rc == 0) {
        *ind_block_ptr = ind_block; 
    }

    kfree(table);
    return rc;
}

static int ext2_bmap_set(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t lbn, uint32_t phys) {
    uint32_t ppb = PTRS_PER_BLOCK(fs);

    if (lbn < EXT2_NDIR_BLOCKS) {
        inode->i_block[lbn] = phys;
        return 0;
    }
    lbn -= EXT2_NDIR_BLOCKS;

    if (lbn < ppb) {
        uint32_t ind_block = inode->i_block[EXT2_IND_BLOCK];
        int rc = ext2_set_indirect(fs, inode, &ind_block, lbn, phys);
        if (rc == 0) {
            inode->i_block[EXT2_IND_BLOCK] = ind_block;
        }
        return rc;
    }
    lbn -= ppb;

    if (lbn < ppb * ppb) {
        uint32_t *l1_table = kmalloc(fs->block_size);
        if (!l1_table) return -ENOMEM;
        
        uint32_t dind_block = inode->i_block[EXT2_DIND_BLOCK];
        
        if (dind_block == 0) {
            dind_block = ext2_alloc_block(fs);
            if (dind_block == 0) { kfree(l1_table); return -ENOSPC; }
            memset(l1_table, 0, fs->block_size);
            inode->i_blocks += fs->sectors_per_block;
        } else {
            if (ext2_read_block(fs, dind_block, l1_table) != 0) {
                kfree(l1_table); return -EIO;
            }
        }
        
        uint32_t l1_idx = lbn / ppb;
        int rc = ext2_set_indirect(fs, inode, &l1_table[l1_idx], lbn % ppb, phys);
        
        if (rc == 0) {
            rc = ext2_write_block(fs, dind_block, l1_table);
        }
        
        if (rc == 0) {
            inode->i_block[EXT2_DIND_BLOCK] = dind_block;
        }
        
        kfree(l1_table);
        return rc;
    }

    lbn -= ppb * ppb;
    uint32_t *l1_table = kmalloc(fs->block_size);
    if (!l1_table) return -ENOMEM;

    uint32_t tind_block = inode->i_block[EXT2_TIND_BLOCK];

    if (tind_block == 0) {
        tind_block = ext2_alloc_block(fs);
        if (tind_block == 0) { kfree(l1_table); return -ENOSPC; }
        memset(l1_table, 0, fs->block_size);
        inode->i_blocks += fs->sectors_per_block;
    } else {
        if (ext2_read_block(fs, tind_block, l1_table) != 0) { 
            kfree(l1_table); return -EIO; 
        }
    }

    uint32_t l1_idx = lbn / (ppb * ppb);
    uint32_t *l2_table = kmalloc(fs->block_size);
    if (!l2_table) { kfree(l1_table); return -ENOMEM; }

    uint32_t l1_entry = l1_table[l1_idx];
    if (l1_entry == 0) {
        l1_entry = ext2_alloc_block(fs);
        if (l1_entry == 0) { 
            kfree(l1_table); kfree(l2_table); return -ENOSPC; 
        }
        memset(l2_table, 0, fs->block_size);
        inode->i_blocks += fs->sectors_per_block;
    } else {
        if (ext2_read_block(fs, l1_entry, l2_table) != 0) {
            kfree(l1_table); kfree(l2_table); return -EIO;
        }
    }

    uint32_t l2_idx = (lbn / ppb) % ppb;
    int rc = ext2_set_indirect(fs, inode, &l2_table[l2_idx], lbn % ppb, phys);

    if (rc == 0) {
        rc = ext2_write_block(fs, l1_entry, l2_table);
    }

    if (rc == 0) {
        l1_table[l1_idx] = l1_entry;
        rc = ext2_write_block(fs, tind_block, l1_table);
    }

    if (rc == 0) {
        inode->i_block[EXT2_TIND_BLOCK] = tind_block;
    }

    kfree(l1_table);
    kfree(l2_table);
    return rc;
}

static uint32_t ext2_alloc_inode(ext2_fs_t *fs) {
    for (uint32_t g = 0; g < fs->num_groups; g++) {
        if (fs->bgdt[g].bg_free_inodes_count == 0) continue;

        uint8_t *bitmap = kmalloc(fs->block_size);
        if (!bitmap) return 0;
        if (ext2_read_block(fs, fs->bgdt[g].bg_inode_bitmap, bitmap) != 0) {
            kfree(bitmap);
            continue;
        }
        for (uint32_t i = 0; i < fs->inodes_per_group; i++) {
            if (!(bitmap[i / 8] & (1u << (i % 8)))) {
                bitmap[i / 8] |= (1u << (i % 8));
                ext2_write_block(fs, fs->bgdt[g].bg_inode_bitmap, bitmap);
                kfree(bitmap);
                uint32_t ino = g * fs->inodes_per_group + i + 1;
                fs->bgdt[g].bg_free_inodes_count--;
                fs->sb.s_free_inodes_count--;
                return ino;
            }
        }
        kfree(bitmap);
    }
    return 0;
}

static void ext2_free_inode(ext2_fs_t *fs, uint32_t ino) {
    if (ino == 0) return;
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    if (group >= fs->num_groups) return;

    uint8_t *bitmap = kmalloc(fs->block_size);
    if (!bitmap) return;
    if (ext2_read_block(fs, fs->bgdt[group].bg_inode_bitmap, bitmap) != 0) {
        kfree(bitmap);
        return;
    }
    bitmap[index / 8] &= ~(1u << (index % 8));
    ext2_write_block(fs, fs->bgdt[group].bg_inode_bitmap, bitmap);
    kfree(bitmap);
    fs->bgdt[group].bg_free_inodes_count++;
    fs->sb.s_free_inodes_count++;
}

static uint32_t ext2_dir_lookup(ext2_fs_t *fs, uint32_t dir_ino, const char *name) {
    ext2_inode_t inode;
    if (ext2_read_inode(fs, dir_ino, &inode) != 0) return 0;
    if (!EXT2_ISDIR(inode.i_mode)) return 0;

    uint32_t size     = inode.i_size;
    uint32_t lbn      = 0;
    uint32_t consumed = 0;
    size_t   namelen  = strlen(name);
    uint8_t *blk      = kmalloc(fs->block_size);
    if (!blk) return 0;

    while (consumed < size) {
        uint32_t phys = ext2_bmap(fs, &inode, lbn);
        if (phys == 0) { consumed += fs->block_size; lbn++; continue; }
        if (ext2_read_block(fs, phys, blk) != 0) break;

        uint32_t off = 0;
        uint32_t chunk = fs->block_size;
        if (consumed + chunk > size) chunk = size - consumed;

        while (off < chunk) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len < 8) break;
            if (de->inode != 0 && de->name_len == (uint8_t)namelen &&
                strncmp(de->name, name, namelen) == 0) {
                uint32_t found = de->inode;
                kfree(blk);
                return found;
            }
            off += de->rec_len;
        }
        consumed += fs->block_size;
        lbn++;
    }
    kfree(blk);
    return 0;
}

static uint32_t ext2_resolve_path(ext2_fs_t *fs, const char *path) {
    if (!path || path[0] != '/') return 0;

    uint32_t ino = EXT2_ROOT_INO;
    char buf[256];
    size_t plen = strlen(path);
    if (plen >= sizeof(buf)) return 0;
    memcpy(buf, path, plen + 1);

    char *p = buf + 1;
    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        char *end = p;
        while (*end && *end != '/') end++;
        char saved = *end;
        *end = '\0';

        ino = ext2_dir_lookup(fs, ino, p);
        if (ino == 0) return 0;

        *end = saved;
        p = end;
    }
    return ino;
}

static uint32_t ext2_resolve_parent(ext2_fs_t *fs, const char *path, char *name_out) {
    char buf[256];
    size_t plen = strlen(path);
    if (plen == 0 || plen >= sizeof(buf)) return 0;
    memcpy(buf, path, plen + 1);

    while (plen > 1 && buf[plen - 1] == '/') { buf[--plen] = '\0'; }
    char *slash = buf + plen - 1;
    while (slash > buf && *slash != '/') slash--;

    if (slash == buf) {
        strcpy_safe(name_out, buf + 1, 256);
        return EXT2_ROOT_INO;
    }
    *slash = '\0';
    strcpy_safe(name_out, slash + 1, 256);
    return ext2_resolve_path(fs, buf);
}

static int ext2_dir_add_entry(ext2_fs_t *fs, uint32_t dir_ino, uint32_t ino, 
                              const char *name, uint8_t file_type) {
    ext2_inode_t dir_inode;
    if (ext2_read_inode(fs, dir_ino, &dir_inode) != 0) return -EIO;
    if (!EXT2_ISDIR(dir_inode.i_mode)) return -ENOTDIR;

    size_t   namelen  = strlen(name);
    uint16_t needed   = (uint16_t)((8 + namelen + 3) & ~3u);
    uint32_t size     = dir_inode.i_size;
    uint32_t lbn      = 0;
    uint8_t *blk      = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    uint32_t consumed = 0;
    while (consumed < size) {
        uint32_t phys = ext2_bmap(fs, &dir_inode, lbn);
        bool new_block = (phys == 0);
        if (new_block) {
            phys = ext2_alloc_block(fs);
            if (phys == 0) { kfree(blk); return -ENOSPC; }
            memset(blk, 0, fs->block_size);
            ext2_dirent_t *span = (ext2_dirent_t *)blk;
            span->inode    = 0;
            span->rec_len  = (uint16_t)fs->block_size;
            span->name_len = 0;
            span->file_type = 0;
        } else {
            if (ext2_read_block(fs, phys, blk) != 0) { kfree(blk); return -EIO; }
        }

        uint32_t off = 0;
        while (off < fs->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len == 0) break;

            uint16_t real_len = (de->inode == 0) ? 0
                : (uint16_t)((8 + de->name_len + 3) & ~3u);
            uint16_t free_space = de->rec_len - real_len;

            if (de->inode == 0) free_space = de->rec_len;

            if (free_space >= needed) {
                if (de->inode != 0) {
                    uint16_t old_rec = de->rec_len;
                    de->rec_len = real_len;
                    off += real_len;
                    de = (ext2_dirent_t *)(blk + off);
                    de->rec_len = old_rec - real_len;
                }
                de->inode     = ino;
                de->name_len  = (uint8_t)namelen;
                de->file_type = file_type;
                memcpy(de->name, name, namelen);

                if (new_block) {
                    ext2_bmap_set(fs, &dir_inode, lbn, phys);
                    dir_inode.i_size += fs->block_size;
                    dir_inode.i_blocks += fs->sectors_per_block;
                }
                ext2_write_block(fs, phys, blk);
                ext2_write_inode(fs, dir_ino, &dir_inode);
                kfree(blk);
                return 0;
            }
            off += de->rec_len;
        }
        consumed += fs->block_size;
        lbn++;
    }

    uint32_t phys = ext2_alloc_block(fs);
    if (phys == 0) { kfree(blk); return -ENOSPC; }
    memset(blk, 0, fs->block_size);
    ext2_dirent_t *de = (ext2_dirent_t *)blk;
    de->inode     = ino;
    de->rec_len   = (uint16_t)fs->block_size;
    de->name_len  = (uint8_t)namelen;
    de->file_type = file_type;
    memcpy(de->name, name, namelen);
    ext2_bmap_set(fs, &dir_inode, lbn, phys);
    dir_inode.i_size  += fs->block_size;
    dir_inode.i_blocks += fs->sectors_per_block;
    ext2_write_block(fs, phys, blk);
    ext2_write_inode(fs, dir_ino, &dir_inode);
    kfree(blk);
    return 0;
}

static int ext2_dir_remove_entry(ext2_fs_t *fs, uint32_t dir_ino, const char *name) {
    ext2_inode_t dir_inode;
    if (ext2_read_inode(fs, dir_ino, &dir_inode) != 0) return -EIO;

    size_t   namelen  = strlen(name);
    uint32_t size     = dir_inode.i_size;
    uint32_t lbn      = 0;
    uint32_t consumed = 0;
    uint8_t *blk      = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    while (consumed < size) {
        uint32_t phys = ext2_bmap(fs, &dir_inode, lbn);
        if (phys == 0) { consumed += fs->block_size; lbn++; continue; }
        if (ext2_read_block(fs, phys, blk) != 0) { lbn++; consumed += fs->block_size; continue; }

        uint32_t off = 0;
        ext2_dirent_t *prev = NULL;
        while (off < fs->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == (uint8_t)namelen &&
                strncmp(de->name, name, namelen) == 0) {
                if (prev) {
                    prev->rec_len += de->rec_len;
                } else {
                    de->inode = 0;
                }
                ext2_write_block(fs, phys, blk);
                kfree(blk);
                return 0;
            }
            prev = de;
            off += de->rec_len;
        }
        consumed += fs->block_size;
        lbn++;
    }
    kfree(blk);
    return -ENOENT;
}

int ext2_mount(vfs_mount_t *mnt, const char *device, void *data) {
    (void)data;
    block_device_t *bdev = find_block_device(device);
    if (!bdev) {
        LOG_WARN("ext2: block device '%s' not found\n", device);
        return -ENODEV;
    }

    ext2_fs_t *fs = kmalloc(sizeof(ext2_fs_t));
    if (!fs) return -ENOMEM;
    memset(fs, 0, sizeof(ext2_fs_t));
    fs->block_dev = bdev;

    uint8_t sb_buf[1024];
    if (bdev->ops.read_blocks(bdev, 2, 2, sb_buf) != 0) {
        LOG_WARN("ext2: failed to read superblock from '%s'\n", device);
        kfree(fs);
        return -EIO;
    }
    memcpy(&fs->sb, sb_buf, sizeof(ext2_superblock_t));

    if (fs->sb.s_magic != EXT2_MAGIC) {
        LOG_WARN("ext2: bad magic on '%s' (0x%x)\n", device, fs->sb.s_magic);
        kfree(fs);
        return -EINVAL;
    }

    fs->block_size = 1024u << fs->sb.s_log_block_size;
    fs->sectors_per_block = fs->block_size / 512;
    fs->blocks_per_group  = fs->sb.s_blocks_per_group;
    fs->inodes_per_group  = fs->sb.s_inodes_per_group;
    fs->inode_size = (fs->sb.s_rev_level >= EXT2_DYNAMIC_REV)
                     ? fs->sb.s_inode_size : EXT2_GOOD_OLD_INODE_SIZE;
    fs->first_ino  = (fs->sb.s_rev_level >= EXT2_DYNAMIC_REV)
                     ? fs->sb.s_first_ino : EXT2_GOOD_OLD_FIRST_INO;
    fs->num_groups = (fs->sb.s_blocks_count + fs->blocks_per_group - 1)
                     / fs->blocks_per_group;
    fs->read_only  = (mnt->flags & VFS_MNT_READONLY) != 0;

    uint32_t bgdt_bytes  = fs->num_groups * sizeof(ext2_bgd_t);
    fs->bgdt = kmalloc(bgdt_bytes);
    if (!fs->bgdt) { kfree(fs); return -ENOMEM; }

    uint32_t bgdt_block = (fs->block_size == 1024) ? 2 : 1;
    uint32_t bgdt_blks  = (bgdt_bytes + fs->block_size - 1) / fs->block_size;
    uint8_t *bgdt_buf   = kmalloc(bgdt_blks * fs->block_size);
    if (!bgdt_buf) { kfree(fs->bgdt); kfree(fs); return -ENOMEM; }

    for (uint32_t i = 0; i < bgdt_blks; i++) {
        if (ext2_read_block(fs, bgdt_block + i, bgdt_buf + i * fs->block_size) != 0) {
            kfree(bgdt_buf); kfree(fs->bgdt); kfree(fs);
            return -EIO;
        }
    }
    memcpy(fs->bgdt, bgdt_buf, bgdt_bytes);
    kfree(bgdt_buf);

    mnt->fs_private = fs;
    LOG_INFO("ext2: mounted '%s' at '%s' (block_size=%u, groups=%u)\n",
             device, mnt->mount_point, fs->block_size, fs->num_groups);
    return 0;
}

int ext2_unmount(vfs_mount_t *mnt) {
    ext2_fs_t *fs = (ext2_fs_t *)mnt->fs_private;
    if (!fs) return 0;
    if (!fs->read_only) {
        ext2_write_superblock(fs);
        ext2_write_bgdt(fs);
    }
    kfree(fs->bgdt);
    kfree(fs);
    mnt->fs_private = NULL;
    return 0;
}

int ext2_vfs_open(vfs_mount_t *mnt, const char *path, int flags, vfs_file_handle_t *h) {
    ext2_fs_t *fs = (ext2_fs_t *)mnt->fs_private;
    uint32_t ino = ext2_resolve_path(fs, path);

    if (ino == 0) {
        if (!(flags & VFS_CREAT)) return -ENOENT;
        if (fs->read_only) return -EROFS;

        char name[256];
        uint32_t parent = ext2_resolve_parent(fs, path, name);
        if (parent == 0) return -ENOENT;

        ino = ext2_alloc_inode(fs);
        if (ino == 0) return -ENOSPC;

        ext2_inode_t new_inode;
        memset(&new_inode, 0, sizeof(new_inode));
        new_inode.i_mode       = EXT2_S_IFREG | 0644;
        new_inode.i_links_count = 1;
        new_inode.i_size = 0;
        ext2_write_inode(fs, ino, &new_inode);
        ext2_write_bgdt(fs);
        ext2_write_superblock(fs);

        int rc = ext2_dir_add_entry(fs, parent, ino, name, EXT2_FT_REG_FILE);
        if (rc != 0) {
            ext2_free_inode(fs, ino);
            return rc;
        }
    }

    ext2_inode_t inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return -EIO;

    if (EXT2_ISDIR(inode.i_mode)) return -EISDIR;

    if ((flags & VFS_TRUNC) && !fs->read_only) {
        uint32_t total_lblocks = (inode.i_size + fs->block_size - 1) / fs->block_size;
        for (uint32_t lb = 0; lb < total_lblocks; lb++) {
            uint32_t phys = ext2_bmap(fs, &inode, lb);
            if (phys) ext2_free_block(fs, phys);
        }
        memset(inode.i_block, 0, sizeof(inode.i_block));
        inode.i_size   = 0;
        inode.i_blocks = 0;
        ext2_write_inode(fs, ino, &inode);
    }

    ext2_file_handle_t *fh = kmalloc(sizeof(ext2_file_handle_t));
    if (!fh) return -ENOMEM;
    fh->ino       = ino;
    fh->file_size = inode.i_size;

    h->private_data = fh;
    h->position     = (flags & VFS_APPEND) ? inode.i_size : 0;
    return 0;
}

int ext2_vfs_close(vfs_mount_t *mnt, vfs_file_handle_t *h) {
    (void)mnt;
    if (h->private_data) {
        kfree(h->private_data);
        h->private_data = NULL;
    }
    return 0;
}

vfs_ssize_t ext2_vfs_read(vfs_mount_t *mnt, vfs_file_handle_t *h, void *buf, size_t count) {
    ext2_fs_t *fs = (ext2_fs_t *)mnt->fs_private;
    ext2_file_handle_t *fh = (ext2_file_handle_t *)h->private_data;
    if (!fh) return -EBADF;

    ext2_inode_t inode;
    if (ext2_read_inode(fs, fh->ino, &inode) != 0) return -EIO;

    uint32_t file_size = inode.i_size;
    if ((uint32_t)h->position >= file_size) return 0;

    if ((uint32_t)h->position + count > file_size)
        count = file_size - (uint32_t)h->position;
    if (count == 0) return 0;

    uint8_t *blk = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    size_t   remaining = count;
    uint8_t *dst       = (uint8_t *)buf;
    uint32_t pos       = (uint32_t)h->position;

    while (remaining > 0) {
        uint32_t lbn     = pos / fs->block_size;
        uint32_t boff    = pos % fs->block_size;
        uint32_t can_read = fs->block_size - boff;
        if (can_read > remaining) can_read = (uint32_t)remaining;

        uint32_t phys = ext2_bmap(fs, &inode, lbn);
        if (phys == 0) {
            memset(dst, 0, can_read);
        } else {
            if (ext2_read_block(fs, phys, blk) != 0) {
                kfree(blk);
                return -EIO;
            }
            memcpy(dst, blk + boff, can_read);
        }
        dst       += can_read;
        pos       += can_read;
        remaining -= can_read;
    }
    kfree(blk);
    h->position = pos;
    return (vfs_ssize_t)count;
}

vfs_ssize_t ext2_vfs_write(vfs_mount_t *mnt, vfs_file_handle_t *h, const void *buf, size_t count) {
    ext2_fs_t *fs = (ext2_fs_t *)mnt->fs_private;
    ext2_file_handle_t *fh = (ext2_file_handle_t *)h->private_data;
    if (!fh) return -EBADF;
    if (fs->read_only) return -EROFS;
    if (count == 0) return 0;

    ext2_inode_t inode;
    if (ext2_read_inode(fs, fh->ino, &inode) != 0) return -EIO;

    uint8_t       *blk       = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    size_t         remaining = count;
    const uint8_t *src       = (const uint8_t *)buf;
    uint32_t       pos       = (uint32_t)h->position;

    while (remaining > 0) {
        uint32_t lbn     = pos / fs->block_size;
        uint32_t boff    = pos % fs->block_size;
        uint32_t can_write = fs->block_size - boff;
        if (can_write > remaining) can_write = (uint32_t)remaining;

        uint32_t phys = ext2_bmap(fs, &inode, lbn);
        if (phys == 0) {
            phys = ext2_alloc_block(fs);
            if (phys == 0) { kfree(blk); return -ENOSPC; }
            memset(blk, 0, fs->block_size);
            inode.i_blocks += fs->sectors_per_block;
            int rc = ext2_bmap_set(fs, &inode, lbn, phys);
            if (rc != 0) { kfree(blk); return rc; }
        } else {
            if (boff != 0 || can_write != fs->block_size) {
                if (ext2_read_block(fs, phys, blk) != 0) {
                    kfree(blk);
                    return -EIO;
                }
            }
        }
        memcpy(blk + boff, src, can_write);
        if (ext2_write_block(fs, phys, blk) != 0) {
            kfree(blk); return -EIO;
        }
        src       += can_write;
        pos       += can_write;
        remaining -= can_write;
    }
    kfree(blk);

    if (pos > inode.i_size) {
        inode.i_size = pos;
    }
    h->position = pos;
    ext2_write_inode(fs, fh->ino, &inode);
    return (vfs_ssize_t)count;
}

vfs_off_t ext2_vfs_seek(vfs_mount_t *mnt, vfs_file_handle_t *h, vfs_off_t offset, int whence) {
    (void)mnt;
    ext2_file_handle_t *fh = (ext2_file_handle_t *)h->private_data;
    if (!fh) return -EBADF;

    ext2_inode_t inode;
    if (ext2_read_inode((ext2_fs_t *)mnt->fs_private, fh->ino, &inode) != 0) return -EIO;

    vfs_off_t new_pos;
    switch (whence) {
        case VFS_SEEK_SET: new_pos = offset; break;
        case VFS_SEEK_CUR: new_pos = h->position + offset; break;
        case VFS_SEEK_END: new_pos = (vfs_off_t)inode.i_size + offset; break;
        default: return -EINVAL;
    }
    if (new_pos < 0) return -EINVAL;
    h->position = new_pos;
    return new_pos;
}

int ext2_vfs_readdir(vfs_mount_t *mnt, const char *path, vfs_dirent_t *entries, size_t max_entries) {
    ext2_fs_t *fs = (ext2_fs_t *)mnt->fs_private;
    uint32_t ino = ext2_resolve_path(fs, path);
    if (ino == 0) return -ENOENT;

    ext2_inode_t inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return -EIO;
    if (!EXT2_ISDIR(inode.i_mode)) return -ENOTDIR;

    uint32_t size = inode.i_size;
    if (size > 64 * 1024 * 1024) size = 0;

    uint32_t lbn      = 0;
    uint32_t consumed = 0;
    size_t   count    = 0;
    uint8_t *blk      = kmalloc(fs->block_size);
    if (!blk) return -ENOMEM;

    while (consumed < size && count < max_entries) {
        uint32_t phys = ext2_bmap(fs, &inode, lbn);
        if (phys == 0) { consumed += fs->block_size; lbn++; continue; }
        if (ext2_read_block(fs, phys, blk) != 0) break;

        uint32_t off = 0;
        uint32_t chunk = fs->block_size;
        if (consumed + chunk > size) chunk = size - consumed;

        while (off < chunk && count < max_entries) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len < 8 || (de->rec_len & 3) != 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                size_t nl = de->name_len;
                if (nl >= MAX_FILENAME) nl = MAX_FILENAME - 1;
                memcpy(entries[count].d_name, de->name, nl);
                entries[count].d_name[nl] = '\0';
                entries[count].d_type = (de->file_type == EXT2_FT_DIR)
                                        ? VFS_TYPE_DIR : VFS_TYPE_FILE;
                count++;
            }
            off += de->rec_len;
        }
        consumed += fs->block_size;
        lbn++;
    }
    kfree(blk);
    return (int)count;
}

int ext2_vfs_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *stat) {
    ext2_fs_t *fs = (ext2_fs_t *)mnt->fs_private;
    uint32_t ino = ext2_resolve_path(fs, path);
    if (ino == 0) return -ENOENT;

    ext2_inode_t inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return -EIO;

    stat->st_mode    = inode.i_mode;
    stat->st_size    = inode.i_size;
    stat->st_blksize = fs->block_size;
    stat->st_mtime   = inode.i_mtime;
    return 0;
}

static const vfs_fs_ops_t ext2_ops = {
    .name = "ext2",
    .mount = ext2_mount,
    .unmount = ext2_unmount,
    .open = ext2_vfs_open,
    .close = ext2_vfs_close,
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .seek = ext2_vfs_seek,
    .mkdir = NULL,
    .rmdir = NULL,
    .readdir = ext2_vfs_readdir,
    .stat = ext2_vfs_stat,
    .unlink = NULL,
    .ioctl = NULL,
    .sync = NULL,
};

void ext2_init(void) {
    int rc = vfs_register_filesystem("ext2", &ext2_ops, 0);
    if (rc == 0)
        LOG_INFO("ext2: driver registered\n");
    else
        LOG_WARN("ext2: failed to register driver (%d)\n", rc);
}
