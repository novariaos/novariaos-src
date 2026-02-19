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
    if (cluster < 2 || cluster >= fs->total_clusters + 2) {
        LOG_ERROR("fat32_read_cluster: invalid cluster %u (valid range: 2..%u)\n",
                  cluster, fs->total_clusters + 1);
        return -EINVAL;
    }

    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return fs->block_dev->ops.read_blocks(fs->block_dev, sector,
                                          fs->sectors_per_cluster, buffer);
}

int fat32_write_cluster(fat32_fs_t* fs, uint32_t cluster, const void* buffer) {
    if (!fs || !buffer) return -EINVAL;
    if (cluster < 2 || cluster >= fs->total_clusters + 2) {
        LOG_ERROR("fat32_write_cluster: invalid cluster %u (valid range: 2..%u)\n",
                  cluster, fs->total_clusters + 1);
        return -EINVAL;
    }

    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return fs->block_dev->ops.write_blocks(fs->block_dev, sector,
                                           fs->sectors_per_cluster, buffer);
}

// --- Directory entry reading ---

/*
 * fat32_parse_83_name - Convert an 8.3 FAT name (space-padded, no dot) to a
 * NUL-terminated string.  Returns the number of characters written (excl. NUL).
 */
static int fat32_parse_83_name(const char raw_name[8], const char raw_ext[3],
                                char* out, size_t out_size) {
    if (!out || out_size == 0) return -EINVAL;

    int pos = 0;

    // Copy base name, strip trailing spaces
    int base_len = 8;
    while (base_len > 0 && raw_name[base_len - 1] == ' ')
        base_len--;

    for (int i = 0; i < base_len && pos < (int)out_size - 1; i++)
        out[pos++] = raw_name[i];

    // Append dot + extension if extension is non-empty
    int ext_len = 3;
    while (ext_len > 0 && raw_ext[ext_len - 1] == ' ')
        ext_len--;

    if (ext_len > 0 && pos < (int)out_size - 1) {
        out[pos++] = '.';
        for (int i = 0; i < ext_len && pos < (int)out_size - 1; i++)
            out[pos++] = raw_ext[i];
    }

    out[pos] = '\0';
    return pos;
}

/*
 * fat32_lfn_checksum - Compute the 8.3-name checksum used to verify LFN entries.
 */
static uint8_t fat32_lfn_checksum(const uint8_t name83[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name83[i];
    return sum;
}

/*
 * fat32_lfn_extract - Extract the 13 UCS-2 characters from one LFN entry and
 * store them as ASCII (characters > 127 replaced with '?').
 */
static void fat32_lfn_extract(const fat32_lfn_entry_t* lfn,
                               char out[13], size_t* out_len) {
    *out_len = 0;
    const uint16_t* parts[3];
    const int lens[3] = {5, 6, 2};
    uint16_t buf[13];

    // Copy the three name fields into a flat buffer
    for (int i = 0; i < 5; i++)  buf[i]     = le16_to_cpu(lfn->name1[i]);
    for (int i = 0; i < 6; i++)  buf[5 + i]  = le16_to_cpu(lfn->name2[i]);
    for (int i = 0; i < 2; i++)  buf[11 + i] = le16_to_cpu(lfn->name3[i]);
    (void)parts; (void)lens;

    for (int i = 0; i < 13; i++) {
        uint16_t ch = buf[i];
        if (ch == 0x0000 || ch == 0xFFFF)
            break;
        out[(*out_len)++] = (ch < 0x80) ? (char)ch : '?';
    }
}

/*
 * fat32_read_dir - Read all valid directory entries from a directory whose
 * first cluster is @dir_cluster.  Fills @entries[] (at most @max_entries),
 * and stores the number of entries found in *@out_count.
 *
 * LFN sequences are reconstructed; the resulting long name replaces the
 * 8.3 short name in the parsed entry.
 *
 * Returns 0 on success, negative error code otherwise.
 */
int fat32_read_dir(fat32_fs_t* fs, uint32_t dir_cluster,
                   fat32_entry_t* entries, size_t max_entries,
                   size_t* out_count) {
    if (!fs || !entries || !out_count || max_entries == 0) return -EINVAL;

    *out_count = 0;

    // Allocate a buffer for one cluster
    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    // LFN accumulator: up to 20 entries * 13 chars = 260 chars
    char lfn_buf[FAT_LFN_MAX];
    int  lfn_len       = 0;
    uint8_t lfn_chksum = 0;
    bool have_lfn      = false;

    uint32_t cluster = dir_cluster;
    size_t max_iterations = fs->total_clusters; // Prevent infinite loops on corrupted FAT

    while (cluster >= 2 && !fat32_is_eoc(cluster) && !fat32_is_bad(cluster) && max_iterations-- > 0) {
        int rc = fat32_read_cluster(fs, cluster, cluster_buf);
        if (rc != 0) {
            kfree(cluster_buf);
            return rc;
        }

        size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);

        for (size_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* de = (fat32_dir_entry_t*)(cluster_buf +
                                    i * sizeof(fat32_dir_entry_t));
            uint8_t first = (uint8_t)de->name[0];

            // End of directory
            if (first == FAT_ENTRY_END)
                goto done;

            // Deleted / free entry
            if (first == FAT_ENTRY_FREE) {
                have_lfn = false;
                lfn_len  = 0;
                continue;
            }

            // LFN entry
            if ((de->attr & FAT_ATTR_LONG_NAME_MASK) == FAT_ATTR_LONG_NAME) {
                fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)de;
                uint8_t seq = lfn->order & FAT_LFN_SEQ_MASK;
                if (seq == 0 || seq > 20) {
                    // Invalid sequence number, reset
                    have_lfn = false;
                    lfn_len  = 0;
                    continue;
                }

                if (lfn->order & FAT_LFN_LAST) {
                    // First physical LFN entry (last logical), reset accumulator
                    lfn_len   = 0;
                    lfn_chksum = lfn->checksum;
                    have_lfn  = true;
                    // Pre-fill lfn_buf with spaces so we can place chars by index
                    memset(lfn_buf, 0, sizeof(lfn_buf));
                }

                if (!have_lfn || lfn->checksum != lfn_chksum) {
                    // Checksum mismatch, discard accumulated LFN
                    have_lfn = false;
                    lfn_len  = 0;
                    continue;
                }

                // Extract 13 chars and write them at position (seq-1)*13
                char chunk[13];
                size_t chunk_len = 0;
                fat32_lfn_extract(lfn, chunk, &chunk_len);

                int base = (seq - 1) * 13;
                for (size_t k = 0; k < chunk_len; k++) {
                    if (base + (int)k < FAT_LFN_MAX - 1)
                        lfn_buf[base + k] = chunk[k];
                }
                // Update lfn_len to the furthest written position
                if (base + (int)chunk_len > lfn_len)
                    lfn_len = base + (int)chunk_len;

                continue;
            }

            // Skip volume-ID entries
            if (de->attr & FAT_ATTR_VOLUME_ID) {
                have_lfn = false;
                lfn_len  = 0;
                continue;
            }

            // Regular entry (file or directory)
            if (*out_count >= max_entries) {
                LOG_WARN("fat32_read_dir: entry buffer full, stopping\n");
                goto done;
            }

            fat32_entry_t* out = &entries[(*out_count)++];

            // Determine name
            if (have_lfn && lfn_len > 0) {
                // Validate checksum
                uint8_t expected = fat32_lfn_checksum((const uint8_t*)de->name);
                if (expected == lfn_chksum) {
                    // Use LFN
                    lfn_buf[lfn_len] = '\0';
                    strcpy_safe(out->name, lfn_buf, FAT_LFN_MAX);
                } else {
                    LOG_WARN("fat32_read_dir: LFN checksum mismatch, using 8.3\n");
                    fat32_parse_83_name(de->name, de->ext, out->name, FAT_LFN_MAX);
                }
            } else {
                fat32_parse_83_name(de->name, de->ext, out->name, FAT_LFN_MAX);
            }

            out->first_cluster = ((uint32_t)le16_to_cpu(de->fst_clus_hi) << 16) |
                                  (uint32_t)le16_to_cpu(de->fst_clus_lo);
            out->file_size     = le32_to_cpu(de->file_size);
            out->attr          = de->attr;
            out->is_dir        = (de->attr & FAT_ATTR_DIRECTORY) != 0;

            // Reset LFN state for the next entry
            have_lfn = false;
            lfn_len  = 0;
        }

        // Advance to the next cluster in the chain
        uint32_t next;
        int fat_rc = fat32_read_fat_entry(fs, cluster, &next);
        if (fat_rc != 0) {
            kfree(cluster_buf);
            return fat_rc;
        }
        cluster = next;
    }

done:
    kfree(cluster_buf);
    return 0;
}

/*
 * fat32_lookup - Find a directory entry named @name inside the directory at
 * @dir_cluster.  The comparison is case-insensitive for ASCII characters to
 * match common FAT32 behaviour.
 *
 * Returns 0 and fills *@out on success; -ENOENT if not found.
 */
int fat32_lookup(fat32_fs_t* fs, uint32_t dir_cluster,
                 const char* name, fat32_entry_t* out) {
    if (!fs || !name || !out) return -EINVAL;

    // Allocate temporary entry buffer (max 512 entries per directory scan)
    const size_t MAX_SCAN = 512;
    fat32_entry_t* entries = kmalloc(MAX_SCAN * sizeof(fat32_entry_t));
    if (!entries) return -ENOMEM;

    size_t count = 0;
    int rc = fat32_read_dir(fs, dir_cluster, entries, MAX_SCAN, &count);
    if (rc != 0) {
        kfree(entries);
        return rc;
    }

    for (size_t i = 0; i < count; i++) {
        // Case-insensitive compare
        const char* a = entries[i].name;
        const char* b = name;
        bool match = true;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
            if (ca != cb) { match = false; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') {
            *out = entries[i];
            kfree(entries);
            return 0;
        }
    }

    kfree(entries);
    return -ENOENT;
}

// --- VFS interface helpers ---

/*
 * fat32_resolve_path - Walk @path (relative to mount root) component by
 * component and return the fat32_entry_t for the final component.
 * On success returns 0 and fills *@out.
 */
static int fat32_resolve_path(fat32_fs_t* fs, const char* path,
                               fat32_entry_t* out) {
    if (!fs || !path || !out) return -EINVAL;

    // Start from root cluster
    uint32_t cur_cluster = fs->root_cluster;

    // Skip leading '/'
    while (*path == '/') path++;

    if (*path == '\0') {
        // Root directory itself
        out->first_cluster = cur_cluster;
        out->file_size     = 0;
        out->attr          = FAT_ATTR_DIRECTORY;
        out->is_dir        = true;
        out->name[0]       = '/';
        out->name[1]       = '\0';
        return 0;
    }

    char component[MAX_FILENAME];

    while (*path) {
        // Extract next path component
        int len = 0;
        while (*path && *path != '/' && len < MAX_FILENAME - 1)
            component[len++] = *path++;
        component[len] = '\0';

        // Skip trailing slashes
        while (*path == '/') path++;

        fat32_entry_t entry;
        int rc = fat32_lookup(fs, cur_cluster, component, &entry);
        if (rc != 0) return rc;

        if (*path != '\0' && !entry.is_dir) {
            // Mid-path component is not a directory
            return -ENOTDIR;
        }

        // Validate cluster number before descending
        if (entry.first_cluster == 0) {
            LOG_WARN("fat32_resolve_path: entry '%s' has cluster 0\n", component);
            return -EINVAL;
        }

        cur_cluster = entry.first_cluster;
        *out = entry;
    }

    return 0;
}

int fat32_vfs_readdir(vfs_mount_t* mnt, const char* path,
                      vfs_dirent_t* entries, size_t max_entries) {
    if (!mnt || !mnt->fs_private || !path || !entries) return -EINVAL;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;

    fat32_entry_t dir_entry;
    int rc = fat32_resolve_path(fs, path, &dir_entry);
    if (rc != 0) return rc;

    if (!dir_entry.is_dir) return -ENOTDIR;

    const size_t MAX_SCAN = 512;
    fat32_entry_t* fat_entries = kmalloc(MAX_SCAN * sizeof(fat32_entry_t));
    if (!fat_entries) return -ENOMEM;

    size_t count = 0;
    rc = fat32_read_dir(fs, dir_entry.first_cluster, fat_entries, MAX_SCAN, &count);
    if (rc != 0) {
        kfree(fat_entries);
        return rc;
    }

    size_t filled = 0;
    for (size_t i = 0; i < count && filled < max_entries; i++) {
        // Skip . and .. entries
        if (fat_entries[i].name[0] == '.' &&
            (fat_entries[i].name[1] == '\0' ||
             (fat_entries[i].name[1] == '.' && fat_entries[i].name[2] == '\0')))
            continue;

        strcpy_safe(entries[filled].d_name, fat_entries[i].name, MAX_FILENAME);
        entries[filled].d_type = fat_entries[i].is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        filled++;
    }

    kfree(fat_entries);
    return (int)filled;
}

int fat32_vfs_stat(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat) {
    if (!mnt || !mnt->fs_private || !path || !stat) return -EINVAL;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;

    fat32_entry_t entry;
    int rc = fat32_resolve_path(fs, path, &entry);
    if (rc != 0) return rc;

    if (entry.is_dir) {
        stat->st_mode = VFS_S_IFDIR;
        stat->st_size = 0;
    } else {
        stat->st_mode = VFS_S_IFREG;
        stat->st_size = (vfs_off_t)entry.file_size;
    }

    stat->st_blksize = fs->bytes_per_cluster;
    stat->st_mtime   = 0; // TODO: parse FAT timestamp

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
    .readdir = fat32_vfs_readdir,
    .stat = fat32_vfs_stat,
    .unlink = NULL,
    .ioctl = NULL,
    .sync = NULL,
};
