// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/fat32.h>
#include <core/fs/block.h>
#include <core/fs/vfs.h>
#include <core/kernel/kstd.h>
#include <log.h>
#include <core/kernel/mem.h>
#include <errno.h>
#include <string.h>

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
        return -ENODEV;
    }

    uint8_t* boot_sector = kmalloc(bdev->block_size);
    if (!boot_sector) {
        return -ENOMEM;
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
        return -EINVAL;
    }

    if (strncmp(bpb->fs_type, "FAT32   ", 8) != 0) {
        LOG_WARN("Filesystem type is not 'FAT32': %.8s\n", bpb->fs_type);
    }

    fat32_fs_t* fs_data = kmalloc(sizeof(fat32_fs_t));
    if (!fs_data) {
        kfree(boot_sector);
        return -ENOMEM;
    }

    fs_data->block_dev = bdev;
    fs_data->bytes_per_sector = le16_to_cpu(bpb->bytes_per_sector);
    fs_data->sectors_per_cluster = bpb->sectors_per_cluster;
    fs_data->bytes_per_cluster = fs_data->bytes_per_sector * fs_data->sectors_per_cluster;
    fs_data->reserved_sectors = le16_to_cpu(bpb->reserved_sectors);
    fs_data->num_fats = bpb->num_fats;
    fs_data->fat_size = le32_to_cpu(bpb->sectors_per_fat_32);
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
        return -EINVAL;
    }

    LOG_INFO("FAT32 mounted successfully: %.11s\n", bpb->volume_label);
    LOG_DEBUG("  Bytes/Sector: %u, Sectors/Cluster: %u, Total Clusters: %u\n",
              fs_data->bytes_per_sector, fs_data->sectors_per_cluster,
              fs_data->total_clusters);

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
    uint16_t buf[13];

    // Copy the three name fields into a flat buffer
    for (int i = 0; i < 5; i++)  buf[i]      = le16_to_cpu(lfn->name1[i]);
    for (int i = 0; i < 6; i++)  buf[5 + i]  = le16_to_cpu(lfn->name2[i]);
    for (int i = 0; i < 2; i++)  buf[11 + i] = le16_to_cpu(lfn->name3[i]);

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
                // Update lfn_len to the furthest written position,
                // clamped to FAT_LFN_MAX - 1 to prevent out-of-bounds write
                // on lfn_buf[lfn_len] = '\0' below.
                int new_end = base + (int)chunk_len;
                if (new_end > FAT_LFN_MAX - 1)
                    new_end = FAT_LFN_MAX - 1;
                if (new_end > lfn_len)
                    lfn_len = new_end;

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
                uint8_t name83[11];
                memcpy(name83, de->name, 8);
                memcpy(name83 + 8, de->ext, 3);
                uint8_t expected = fat32_lfn_checksum(name83);
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

        *out = entry;

        if (*path != '\0') {
            if (entry.first_cluster == 0) {
                LOG_WARN("fat32_resolve_path: entry '%s' has cluster 0\n", component);
                return -EINVAL;
            }
            cur_cluster = entry.first_cluster;
        }
    }

    return 0;
}


/*
 * fat32_open_locate_dir_entry - Walk @path to find the parent directory and
 * the exact cluster + index of the final component's on-disk dir entry.
 * Called from fat32_vfs_open so that write can update file_size in place.
 */
static int fat32_open_locate_dir_entry(fat32_fs_t* fs, const char* path,
                                        uint32_t* out_dir_cluster,
                                        uint32_t* out_entry_cluster,
                                        uint32_t* out_entry_index) {
    uint32_t parent_cluster = fs->root_cluster;

    while (*path == '/') path++;

    // Split path into parent components and filename
    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    char component[MAX_FILENAME];

    if (last_slash) {
        const char* p = path;
        while (p < last_slash) {
            int len = 0;
            while (p < last_slash && *p != '/') component[len++] = *p++;
            component[len] = '\0';
            while (*p == '/') p++;

            fat32_entry_t dir_entry;
            int rc = fat32_lookup(fs, parent_cluster, component, &dir_entry);
            if (rc != 0) return rc;
            parent_cluster = dir_entry.first_cluster;
        }
        int len = 0;
        const char* fname = last_slash + 1;
        while (*fname && len < MAX_FILENAME - 1) component[len++] = *fname++;
        component[len] = '\0';
    } else {
        int len = 0;
        while (*path && len < MAX_FILENAME - 1) component[len++] = *path++;
        component[len] = '\0';
    }

    *out_dir_cluster = parent_cluster;

    // Scan raw dir entries cluster by cluster.
    // We track an LFN accumulator in parallel so we can match by long name too,
    // and record the index of the 8.3 entry (not the LFN entries) for later updates.
    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    char lfn_buf[FAT_LFN_MAX];
    int  lfn_len    = 0;
    uint8_t lfn_chk = 0;
    bool have_lfn   = false;

    uint32_t cluster = parent_cluster;
    size_t max_iterations = fs->total_clusters;

    while (cluster >= 2 && !fat32_is_eoc(cluster) && !fat32_is_bad(cluster)
           && max_iterations-- > 0) {
        int rc = fat32_read_cluster(fs, cluster, cluster_buf);
        if (rc != 0) { kfree(cluster_buf); return rc; }

        size_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);

        for (size_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* de = (fat32_dir_entry_t*)(cluster_buf +
                                    i * sizeof(fat32_dir_entry_t));
            uint8_t first = (uint8_t)de->name[0];

            if (first == FAT_ENTRY_END) goto not_found;

            if (first == FAT_ENTRY_FREE) {
                have_lfn = false; lfn_len = 0;
                continue;
            }

            if ((de->attr & FAT_ATTR_LONG_NAME_MASK) == FAT_ATTR_LONG_NAME) {
                fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)de;
                uint8_t seq = lfn->order & FAT_LFN_SEQ_MASK;
                if (seq == 0 || seq > 20) { have_lfn = false; lfn_len = 0; continue; }

                if (lfn->order & FAT_LFN_LAST) {
                    lfn_len  = 0;
                    lfn_chk  = lfn->checksum;
                    have_lfn = true;
                    memset(lfn_buf, 0, sizeof(lfn_buf));
                }

                if (!have_lfn || lfn->checksum != lfn_chk) {
                    have_lfn = false; lfn_len = 0; continue;
                }

                char chunk[13];
                size_t chunk_len = 0;
                fat32_lfn_extract(lfn, chunk, &chunk_len);
                int base = (seq - 1) * 13;
                for (size_t k = 0; k < chunk_len; k++) {
                    if (base + (int)k < FAT_LFN_MAX - 1)
                        lfn_buf[base + k] = chunk[k];
                }
                int new_end = base + (int)chunk_len;
                if (new_end > FAT_LFN_MAX - 1)
                    new_end = FAT_LFN_MAX - 1;
                if (new_end > lfn_len)
                    lfn_len = new_end;
                continue;
            }

            if (de->attr & FAT_ATTR_VOLUME_ID) {
                have_lfn = false; lfn_len = 0; continue;
            }

            // Try LFN match first, then fall back to 8.3
            bool match = false;

            if (have_lfn && lfn_len > 0) {
                uint8_t name83[11];
                memcpy(name83, de->name, 8);
                memcpy(name83 + 8, de->ext, 3);
                if (fat32_lfn_checksum(name83) == lfn_chk) {
                    lfn_buf[lfn_len] = '\0';
                    const char* a = lfn_buf;
                    const char* b = component;
                    match = true;
                    while (*a && *b) {
                        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
                        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
                        if (ca != cb) { match = false; break; }
                        a++; b++;
                    }
                    if (match) match = (*a == '\0' && *b == '\0');
                }
            }

            if (!match) {
                char name83[MAX_FILENAME];
                fat32_parse_83_name(de->name, de->ext, name83, sizeof(name83));
                const char* a = name83;
                const char* b = component;
                match = true;
                while (*a && *b) {
                    char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
                    char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
                    if (ca != cb) { match = false; break; }
                    a++; b++;
                }
                if (match) match = (*a == '\0' && *b == '\0');
            }

            if (match) {
                *out_entry_cluster = cluster;
                *out_entry_index   = (uint32_t)i;
                kfree(cluster_buf);
                return 0;
            }

            have_lfn = false;
            lfn_len  = 0;
        }

        uint32_t next;
        rc = fat32_read_fat_entry(fs, cluster, &next);
        if (rc != 0) { kfree(cluster_buf); return rc; }
        cluster = next;
    }

not_found:
    kfree(cluster_buf);
    return -ENOENT;
}

/*
 * fat32_update_dir_size - Write a new file_size value into the on-disk
 * directory entry identified by (entry_cluster, entry_index).
 */
static int fat32_update_dir_size(fat32_fs_t* fs,
                                  uint32_t entry_cluster, uint32_t entry_index,
                                  uint32_t new_size) {
    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    int rc = fat32_read_cluster(fs, entry_cluster, cluster_buf);
    if (rc != 0) { kfree(cluster_buf); return rc; }

    fat32_dir_entry_t* de = (fat32_dir_entry_t*)(cluster_buf +
                             entry_index * sizeof(fat32_dir_entry_t));
    de->file_size = cpu_to_le32(new_size);

    rc = fat32_write_cluster(fs, entry_cluster, cluster_buf);
    kfree(cluster_buf);
    return rc;
}

/*
 * fat32_resolve_parent_cluster - Split @path into the parent directory's
 * first cluster and the trailing filename component.  The parent directory
 * must already exist.  Returns 0 on success.
 */
static int fat32_resolve_parent_cluster(fat32_fs_t* fs, const char* path,
                                         uint32_t* out_parent_cluster,
                                         const char** out_filename) {
    while (*path == '/') path++;

    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (!last_slash) {
        *out_parent_cluster = fs->root_cluster;
        *out_filename = path;
        return 0;
    }

    int plen = (int)(last_slash - path);
    if (plen >= MAX_FILENAME) return -EINVAL;

    char parent_path[MAX_FILENAME];
    memcpy(parent_path, path, plen);
    parent_path[plen] = '\0';

    fat32_entry_t parent;
    int rc = fat32_resolve_path(fs, parent_path, &parent);
    if (rc != 0) return rc;
    if (!parent.is_dir) return -ENOTDIR;

    *out_parent_cluster = parent.first_cluster;
    *out_filename = last_slash + 1;
    return 0;
}

/*
 * fat32_name_to_83 - Convert a filename to the FAT 8.3 representation
 * (upper-case, space-padded, no dot separator).
 * Returns -EINVAL if the base name exceeds 8 chars or the extension exceeds 3.
 */
static int fat32_name_to_83(const char* name, char out_name[8], char out_ext[3]) {
    memset(out_name, ' ', 8);
    memset(out_ext,  ' ', 3);

    const char* dot = NULL;
    for (const char* p = name; *p; p++) {
        if (*p == '.') dot = p;
    }

    int base_len = dot ? (int)(dot - name) : (int)strlen(name);
    if (base_len == 0 || base_len > 8) return -EINVAL;

    for (int i = 0; i < base_len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out_name[i] = c;
    }

    if (dot && dot[1]) {
        int ext_len = (int)strlen(dot + 1);
        if (ext_len > 3) return -EINVAL;
        for (int i = 0; i < ext_len; i++) {
            char c = dot[1 + i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out_ext[i] = c;
        }
    }

    return 0;
}

/*
 * fat32_create_file_entry - Write a new, empty 8.3 directory entry for
 * @filename into the directory at @dir_cluster.  On success fills
 * *@out_entry_cluster / *@out_entry_index with the on-disk location of
 * the new entry.
 */
static int fat32_create_file_entry(fat32_fs_t* fs, uint32_t dir_cluster,
                                    const char* filename,
                                    uint32_t* out_entry_cluster,
                                    uint32_t* out_entry_index) {
    char name83[8], ext83[3];
    int rc = fat32_name_to_83(filename, name83, ext83);
    if (rc != 0) return rc;

    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    uint32_t cluster = dir_cluster;
    uint32_t last_cluster = dir_cluster;
    size_t max_iterations = fs->total_clusters + 1;

    while (cluster >= 2 && !fat32_is_eoc(cluster) && !fat32_is_bad(cluster)
           && max_iterations-- > 0) {
        rc = fat32_read_cluster(fs, cluster, cluster_buf);
        if (rc != 0) { kfree(cluster_buf); return rc; }

        size_t epc = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);

        for (size_t i = 0; i < epc; i++) {
            fat32_dir_entry_t* de = (fat32_dir_entry_t*)(cluster_buf +
                                    i * sizeof(fat32_dir_entry_t));
            uint8_t first = (uint8_t)de->name[0];

            if (first == FAT_ENTRY_END || first == FAT_ENTRY_FREE) {
                memset(de, 0, sizeof(fat32_dir_entry_t));
                memcpy(de->name, name83, 8);
                memcpy(de->ext,  ext83,  3);
                de->attr      = FAT_ATTR_ARCHIVE;
                de->file_size = 0;

                rc = fat32_write_cluster(fs, cluster, cluster_buf);
                if (rc != 0) { kfree(cluster_buf); return rc; }

                *out_entry_cluster = cluster;
                *out_entry_index   = (uint32_t)i;
                kfree(cluster_buf);
                return 0;
            }
        }

        last_cluster = cluster;
        uint32_t next;
        rc = fat32_read_fat_entry(fs, cluster, &next);
        if (rc != 0) { kfree(cluster_buf); return rc; }
        cluster = next;
    }

    // No free slot found — extend the directory with a new cluster.
    uint32_t new_cluster;
    rc = fat32_extend_chain(fs, last_cluster, &new_cluster);
    if (rc != 0) { kfree(cluster_buf); return rc; }

    memset(cluster_buf, 0, fs->bytes_per_cluster);
    fat32_dir_entry_t* de = (fat32_dir_entry_t*)cluster_buf;
    memcpy(de->name, name83, 8);
    memcpy(de->ext,  ext83,  3);
    de->attr      = FAT_ATTR_ARCHIVE;
    de->file_size = 0;

    rc = fat32_write_cluster(fs, new_cluster, cluster_buf);
    if (rc != 0) {
        fat32_write_fat_entry(fs, new_cluster, FAT32_FREE);
        kfree(cluster_buf);
        return rc;
    }

    *out_entry_cluster = new_cluster;
    *out_entry_index   = 0;
    kfree(cluster_buf);
    return 0;
}

/*
 * fat32_trunc_file - Truncate an open file to zero bytes.
 * Frees all data clusters and resets file_size / first_cluster in the
 * on-disk directory entry.
 */
static int fat32_trunc_file(fat32_fs_t* fs, fat32_file_handle_t* fh) {
    if (fh->first_cluster != 0) {
        int rc = fat32_free_chain(fs, fh->first_cluster);
        if (rc != 0) return rc;
        fh->first_cluster = 0;
    }
    fh->file_size = 0;

    uint8_t* dbuf = kmalloc(fs->bytes_per_cluster);
    if (!dbuf) return -ENOMEM;

    int rc = fat32_read_cluster(fs, fh->dir_entry_cluster, dbuf);
    if (rc == 0) {
        fat32_dir_entry_t* de = (fat32_dir_entry_t*)(dbuf +
                                fh->dir_entry_index * sizeof(fat32_dir_entry_t));
        de->fst_clus_hi = 0;
        de->fst_clus_lo = 0;
        de->file_size   = 0;
        rc = fat32_write_cluster(fs, fh->dir_entry_cluster, dbuf);
    }
    kfree(dbuf);
    return rc;
}

int fat32_vfs_open(vfs_mount_t* mnt, const char* path, int flags,
                   vfs_file_handle_t* h) {
    if (!mnt || !mnt->fs_private || !path || !h) return -EINVAL;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;

    fat32_entry_t entry;
    int rc = fat32_resolve_path(fs, path, &entry);

    if (rc == -ENOENT && (flags & VFS_CREAT)) {
        uint32_t parent_cluster;
        const char* filename;
        rc = fat32_resolve_parent_cluster(fs, path, &parent_cluster, &filename);
        if (rc != 0) return rc;

        uint32_t entry_cluster, entry_index;
        rc = fat32_create_file_entry(fs, parent_cluster, filename,
                                     &entry_cluster, &entry_index);
        if (rc != 0) return rc;

        fat32_file_handle_t* fh = kmalloc(sizeof(fat32_file_handle_t));
        if (!fh) return -ENOMEM;

        fh->first_cluster     = 0;
        fh->file_size         = 0;
        fh->dir_cluster       = parent_cluster;
        fh->dir_entry_cluster = entry_cluster;
        fh->dir_entry_index   = entry_index;

        h->private_data = fh;
        h->position     = 0;

        return 0;
    }

    if (rc != 0) return rc;
    if (entry.is_dir) return -EINVAL;

    fat32_file_handle_t* fh = kmalloc(sizeof(fat32_file_handle_t));
    if (!fh) return -ENOMEM;

    fh->first_cluster = entry.first_cluster;
    fh->file_size     = entry.file_size;

    rc = fat32_open_locate_dir_entry(fs, path,
                                     &fh->dir_cluster,
                                     &fh->dir_entry_cluster,
                                     &fh->dir_entry_index);
    if (rc != 0) {
        kfree(fh);
        return rc;
    }

    h->private_data = fh;
    h->position     = 0;

    if (flags & VFS_TRUNC) {
        rc = fat32_trunc_file(fs, fh);
        if (rc != 0) {
            kfree(fh);
            h->private_data = NULL;
            return rc;
        }
    }

    if (flags & VFS_APPEND)
        h->position = (vfs_off_t)fh->file_size;

    return 0;
}

int fat32_vfs_close(vfs_mount_t* mnt, vfs_file_handle_t* h) {
    if (!h) return -EINVAL;

    if (h->private_data) {
        kfree(h->private_data);
        h->private_data = NULL;
    }
    return 0;
}

vfs_ssize_t fat32_vfs_read(vfs_mount_t* mnt, vfs_file_handle_t* h,
                            void* buf, size_t count) {
    if (!mnt || !mnt->fs_private || !h || !buf) return -EINVAL;
    if (count == 0) return 0;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;
    fat32_file_handle_t* fh = (fat32_file_handle_t*)h->private_data;
    if (!fh) return -EBADF;

    if (h->position < 0 || (uint32_t)h->position >= fh->file_size) return 0;

    uint32_t remaining = fh->file_size - (uint32_t)h->position;
    if (count > remaining) count = remaining;

    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    uint8_t* dst = (uint8_t*)buf;
    size_t copied = 0;
    uint32_t cluster = fh->first_cluster;

    uint32_t cluster_idx = (uint32_t)h->position / fs->bytes_per_cluster;
    for (uint32_t i = 0; i < cluster_idx; i++) {
        if (fat32_is_eoc(cluster) || fat32_is_bad(cluster)) {
            kfree(cluster_buf);
            return -EIO;
        }
        uint32_t next;
        int rc = fat32_read_fat_entry(fs, cluster, &next);
        if (rc != 0) { kfree(cluster_buf); return rc; }
        cluster = next;
    }

    uint32_t offset_in_cluster = (uint32_t)h->position % fs->bytes_per_cluster;

    while (copied < count) {
        if (fat32_is_eoc(cluster) || fat32_is_bad(cluster)) break;

        int rc = fat32_read_cluster(fs, cluster, cluster_buf);
        if (rc != 0) { kfree(cluster_buf); return rc; }

        uint32_t avail = fs->bytes_per_cluster - offset_in_cluster;
        size_t want = count - copied;
        size_t take = want < avail ? want : avail;

        memcpy(dst + copied, cluster_buf + offset_in_cluster, take);
        copied += take;
        offset_in_cluster = 0;

        uint32_t next;
        rc = fat32_read_fat_entry(fs, cluster, &next);
        if (rc != 0) { kfree(cluster_buf); return rc; }
        cluster = next;
    }

    kfree(cluster_buf);
    h->position += (vfs_off_t)copied;
    return (vfs_ssize_t)copied;
}

vfs_ssize_t fat32_vfs_write(vfs_mount_t* mnt, vfs_file_handle_t* h,
                             const void* buf, size_t count) {
    if (!mnt || !mnt->fs_private || !h || !buf) return -EINVAL;
    if (count == 0) return 0;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;
    fat32_file_handle_t* fh = (fat32_file_handle_t*)h->private_data;
    if (!fh) return -EBADF;
    if (h->position < 0) return -EINVAL;

    const uint8_t* src = (const uint8_t*)buf;
    size_t written = 0;
    uint32_t pos = (uint32_t)h->position;

    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    uint32_t cluster_idx = pos / fs->bytes_per_cluster;
    uint32_t offset_in_cluster = pos % fs->bytes_per_cluster;

    uint32_t cluster = fh->first_cluster;

    if (cluster == 0) {
        // Empty file: allocate and zero the very first cluster.
        int rc = fat32_alloc_cluster(fs, &cluster);
        if (rc != 0) { kfree(cluster_buf); return rc; }

        memset(cluster_buf, 0, fs->bytes_per_cluster);
        rc = fat32_write_cluster(fs, cluster, cluster_buf);
        if (rc != 0) {
            fat32_write_fat_entry(fs, cluster, FAT32_FREE);
            kfree(cluster_buf);
            return rc;
        }

        fh->first_cluster = cluster;

        // Persist the new first-cluster number in the directory entry.
        uint8_t* dbuf = kmalloc(fs->bytes_per_cluster);
        if (dbuf) {
            if (fat32_read_cluster(fs, fh->dir_entry_cluster, dbuf) == 0) {
                fat32_dir_entry_t* de = (fat32_dir_entry_t*)(dbuf +
                                        fh->dir_entry_index * sizeof(fat32_dir_entry_t));
                de->fst_clus_hi = cpu_to_le16((uint16_t)(cluster >> 16));
                de->fst_clus_lo = cpu_to_le16((uint16_t)(cluster & 0xFFFF));
                fat32_write_cluster(fs, fh->dir_entry_cluster, dbuf);
            }
            kfree(dbuf);
        }

        // Navigate forward cluster_idx steps, allocating zeroed clusters as needed,
        // so that Phase 2 always starts at the correct cluster.
        for (uint32_t i = 0; i < cluster_idx; i++) {
            uint32_t new_cluster;
            int nav_rc = fat32_extend_chain(fs, cluster, &new_cluster);
            if (nav_rc != 0) { kfree(cluster_buf); return nav_rc; }
            memset(cluster_buf, 0, fs->bytes_per_cluster);
            nav_rc = fat32_write_cluster(fs, new_cluster, cluster_buf);
            if (nav_rc != 0) { kfree(cluster_buf); return nav_rc; }
            cluster = new_cluster;
        }
    } else {
        // Walk cluster_idx steps along the existing chain, extending as needed.
        // After the loop `cluster` is the cluster at index cluster_idx.
        for (uint32_t i = 0; i < cluster_idx; i++) {
            uint32_t next;
            int rc = fat32_read_fat_entry(fs, cluster, &next);
            if (rc != 0) { kfree(cluster_buf); return rc; }

            if (fat32_is_eoc(next)) {
                // Chain too short — allocate a new cluster and link it.
                uint32_t new_cluster;
                rc = fat32_extend_chain(fs, cluster, &new_cluster);
                if (rc != 0) { kfree(cluster_buf); return rc; }

                memset(cluster_buf, 0, fs->bytes_per_cluster);
                rc = fat32_write_cluster(fs, new_cluster, cluster_buf);
                if (rc != 0) { kfree(cluster_buf); return rc; }
                cluster = new_cluster;
            } else if (fat32_is_bad(next)) {
                kfree(cluster_buf);
                return -EIO;
            } else {
                cluster = next;
            }
        }
    }

    while (written < count) {
        if (fat32_is_eoc(cluster) || cluster < 2) {
            // Should not happen after Phase 1, but guard anyway.
            break;
        }

        // Read-modify-write for partial cluster writes.
        int rc = fat32_read_cluster(fs, cluster, cluster_buf);
        if (rc != 0) { kfree(cluster_buf); return (written > 0) ? (vfs_ssize_t)written : rc; }

        uint32_t avail = fs->bytes_per_cluster - offset_in_cluster;
        size_t want = count - written;
        size_t take = want < avail ? want : avail;

        memcpy(cluster_buf + offset_in_cluster, src + written, take);

        rc = fat32_write_cluster(fs, cluster, cluster_buf);
        if (rc != 0) { kfree(cluster_buf); return (written > 0) ? (vfs_ssize_t)written : rc; }

        written += take;
        offset_in_cluster = 0;

        if (written < count) {
            // Advance to the next cluster, allocating if at EOC.
            uint32_t next;
            rc = fat32_read_fat_entry(fs, cluster, &next);
            if (rc != 0) { kfree(cluster_buf); return (vfs_ssize_t)written; }

            if (fat32_is_eoc(next)) {
                uint32_t new_cluster;
                rc = fat32_extend_chain(fs, cluster, &new_cluster);
                if (rc != 0) {
                    kfree(cluster_buf);
                    return (written > 0) ? (vfs_ssize_t)written : rc;
                }
                memset(cluster_buf, 0, fs->bytes_per_cluster);
                rc = fat32_write_cluster(fs, new_cluster, cluster_buf);
                if (rc != 0) {
                    kfree(cluster_buf);
                    return (written > 0) ? (vfs_ssize_t)written : rc;
                }
                cluster = new_cluster;
            } else if (fat32_is_bad(next) || next < 2) {
                break;
            } else {
                cluster = next;
            }
        }
    }

    kfree(cluster_buf);

    h->position += (vfs_off_t)written;

    // Update in-memory file size and flush to disk if we extended the file.
    if ((uint32_t)h->position > fh->file_size) {
        fh->file_size = (uint32_t)h->position;
        fat32_update_dir_size(fs, fh->dir_entry_cluster,
                              fh->dir_entry_index, fh->file_size);
    }

    return (vfs_ssize_t)written;
}

vfs_off_t fat32_vfs_seek(vfs_mount_t* mnt, vfs_file_handle_t* h,
                          vfs_off_t offset, int whence) {
    if (!h) return -EINVAL;

    fat32_file_handle_t* fh = (fat32_file_handle_t*)h->private_data;
    if (!fh) return -EBADF;

    vfs_off_t new_pos;

    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = h->position + offset;
            break;
        case VFS_SEEK_END:
            new_pos = (vfs_off_t)fh->file_size + offset;
            break;
        default:
            return -EINVAL;
    }

    if (new_pos < 0) new_pos = 0;

    h->position = new_pos;
    return new_pos;
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

#ifndef ENOTEMPTY
#define ENOTEMPTY 39
#endif

/*
 * fat32_vfs_mkdir - Create a new directory at @path.
 *
 * Allocates a cluster for the new directory, initialises it with "." and ".."
 * entries, then writes a directory entry with FAT_ATTR_DIRECTORY into the
 * parent directory.
 */
static int fat32_vfs_mkdir(vfs_mount_t* mnt, const char* path, uint32_t mode) {
    (void)mode;
    if (!mnt || !mnt->fs_private || !path) return -EINVAL;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;

    /* Reject if it already exists. */
    fat32_entry_t existing;
    if (fat32_resolve_path(fs, path, &existing) == 0) return -EEXIST;

    /* Split into parent cluster + new directory name. */
    uint32_t parent_cluster;
    const char* dirname;
    int rc = fat32_resolve_parent_cluster(fs, path, &parent_cluster, &dirname);
    if (rc != 0) return rc;

    /* Validate the name is expressible as an 8.3 entry. */
    char name83[8], ext83[3];
    rc = fat32_name_to_83(dirname, name83, ext83);
    if (rc != 0) return rc;

    /* Allocate one cluster to hold the new directory's entries. */
    uint32_t dir_cluster;
    rc = fat32_alloc_cluster(fs, &dir_cluster);
    if (rc != 0) return rc;

    /* Initialise the cluster: "." and ".." as first two entries, rest zeroed. */
    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) {
        fat32_write_fat_entry(fs, dir_cluster, FAT32_FREE);
        return -ENOMEM;
    }
    memset(cluster_buf, 0, fs->bytes_per_cluster);

    fat32_dir_entry_t* dot    = (fat32_dir_entry_t*)cluster_buf;
    fat32_dir_entry_t* dotdot = dot + 1;

    /* "." — points to self. */
    memset(dot->name, ' ', 8); dot->name[0] = '.';
    memset(dot->ext,  ' ', 3);
    dot->attr        = FAT_ATTR_DIRECTORY;
    dot->fst_clus_hi = cpu_to_le16((uint16_t)(dir_cluster >> 16));
    dot->fst_clus_lo = cpu_to_le16((uint16_t)(dir_cluster & 0xFFFF));

    /* ".." — points to parent; root directory is represented as cluster 0. */
    memset(dotdot->name, ' ', 8); dotdot->name[0] = '.'; dotdot->name[1] = '.';
    memset(dotdot->ext,  ' ', 3);
    dotdot->attr = FAT_ATTR_DIRECTORY;
    uint32_t parent_for_dotdot = (parent_cluster == fs->root_cluster) ? 0 : parent_cluster;
    dotdot->fst_clus_hi = cpu_to_le16((uint16_t)(parent_for_dotdot >> 16));
    dotdot->fst_clus_lo = cpu_to_le16((uint16_t)(parent_for_dotdot & 0xFFFF));

    rc = fat32_write_cluster(fs, dir_cluster, cluster_buf);
    kfree(cluster_buf);
    if (rc != 0) {
        fat32_write_fat_entry(fs, dir_cluster, FAT32_FREE);
        return rc;
    }

    /*
     * Write a new entry into the parent directory via fat32_create_file_entry
     * (which finds/extends a free slot), then patch attr and first_cluster.
     */
    uint32_t entry_cluster, entry_index;
    rc = fat32_create_file_entry(fs, parent_cluster, dirname,
                                  &entry_cluster, &entry_index);
    if (rc != 0) {
        fat32_free_chain(fs, dir_cluster);
        return rc;
    }

    cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) {
        fat32_free_chain(fs, dir_cluster);
        return -ENOMEM;
    }

    rc = fat32_read_cluster(fs, entry_cluster, cluster_buf);
    if (rc != 0) { kfree(cluster_buf); fat32_free_chain(fs, dir_cluster); return rc; }

    fat32_dir_entry_t* de = (fat32_dir_entry_t*)(cluster_buf +
                             entry_index * sizeof(fat32_dir_entry_t));
    de->attr        = FAT_ATTR_DIRECTORY;
    de->fst_clus_hi = cpu_to_le16((uint16_t)(dir_cluster >> 16));
    de->fst_clus_lo = cpu_to_le16((uint16_t)(dir_cluster & 0xFFFF));
    de->file_size   = 0;

    rc = fat32_write_cluster(fs, entry_cluster, cluster_buf);
    kfree(cluster_buf);
    if (rc != 0) {
        fat32_free_chain(fs, dir_cluster);
        return rc;
    }

    LOG_DEBUG("fat32_vfs_mkdir: created directory '%s' at cluster %u\n",
              path, dir_cluster);
    return 0;
}

/*
 * fat32_vfs_rmdir - Remove the empty directory at @path.
 *
 * Verifies the directory exists, is a directory, and contains no entries
 * other than "." and "..".  Then marks its parent directory entry as deleted
 * and frees its cluster chain.
 */
static int fat32_vfs_rmdir(vfs_mount_t* mnt, const char* path) {
    if (!mnt || !mnt->fs_private || !path) return -EINVAL;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;

    /* Resolve the target and verify it is a directory. */
    fat32_entry_t entry;
    int rc = fat32_resolve_path(fs, path, &entry);
    if (rc != 0) return rc;
    if (!entry.is_dir) return -ENOTDIR;

    /* Check that the directory is empty (only "." and ".." allowed). */
    const size_t MAX_SCAN = 512;
    fat32_entry_t* dir_entries = kmalloc(MAX_SCAN * sizeof(fat32_entry_t));
    if (!dir_entries) return -ENOMEM;

    size_t count = 0;
    rc = fat32_read_dir(fs, entry.first_cluster, dir_entries, MAX_SCAN, &count);
    if (rc != 0) { kfree(dir_entries); return rc; }

    for (size_t i = 0; i < count; i++) {
        const char* n = dir_entries[i].name;
        bool is_dot    = (n[0] == '.' && n[1] == '\0');
        bool is_dotdot = (n[0] == '.' && n[1] == '.' && n[2] == '\0');
        if (!is_dot && !is_dotdot) {
            kfree(dir_entries);
            return -ENOTEMPTY;
        }
    }
    kfree(dir_entries);

    /* Locate the entry in the parent directory and mark it deleted. */
    uint32_t parent_cluster;
    const char* dirname;
    rc = fat32_resolve_parent_cluster(fs, path, &parent_cluster, &dirname);
    if (rc != 0) return rc;

    char name83[8], ext83[3];
    rc = fat32_name_to_83(dirname, name83, ext83);
    if (rc != 0) return rc;

    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    uint32_t cluster = parent_cluster;
    size_t max_iterations = fs->total_clusters + 1;
    bool found = false;

    while (!found && cluster >= 2 && !fat32_is_eoc(cluster) &&
           !fat32_is_bad(cluster) && max_iterations-- > 0) {
        rc = fat32_read_cluster(fs, cluster, cluster_buf);
        if (rc != 0) { kfree(cluster_buf); return rc; }

        size_t epc = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);

        for (size_t i = 0; i < epc; i++) {
            fat32_dir_entry_t* de = (fat32_dir_entry_t*)(cluster_buf +
                                    i * sizeof(fat32_dir_entry_t));
            uint8_t first = (uint8_t)de->name[0];

            if (first == FAT_ENTRY_END) goto scan_done;
            if (first == FAT_ENTRY_FREE) continue;
            if ((de->attr & FAT_ATTR_LONG_NAME_MASK) == FAT_ATTR_LONG_NAME) continue;
            if (!(de->attr & FAT_ATTR_DIRECTORY)) continue;

            if (memcmp(de->name, name83, 8) == 0 && memcmp(de->ext, ext83, 3) == 0) {
                de->name[0] = (char)FAT_ENTRY_FREE;
                rc = fat32_write_cluster(fs, cluster, cluster_buf);
                if (rc != 0) { kfree(cluster_buf); return rc; }
                found = true;
                break;
            }
        }

        if (!found) {
            uint32_t next;
            rc = fat32_read_fat_entry(fs, cluster, &next);
            if (rc != 0) { kfree(cluster_buf); return rc; }
            cluster = next;
        }
    }

scan_done:
    kfree(cluster_buf);

    if (!found) return -ENOENT;

    /* Free all clusters belonging to the removed directory. */
    if (entry.first_cluster >= 2) {
        rc = fat32_free_chain(fs, entry.first_cluster);
        if (rc != 0) return rc;
    }

    LOG_DEBUG("fat32_vfs_rmdir: removed directory '%s'\n", path);
    return 0;
}

/*
 * fat32_vfs_unlink - Delete the regular file at @path.
 *
 * Verifies the path exists and is a regular file (not a directory).
 * Then marks its parent directory entry as deleted and frees its
 * cluster chain.
 */
static int fat32_vfs_unlink(vfs_mount_t* mnt, const char* path) {
    if (!mnt || !mnt->fs_private || !path) return -EINVAL;

    fat32_fs_t* fs = (fat32_fs_t*)mnt->fs_private;

    /* Resolve the target and verify it is a regular file. */
    fat32_entry_t entry;
    int rc = fat32_resolve_path(fs, path, &entry);
    if (rc != 0) return rc;
    if (entry.is_dir) return -EISDIR;

    /* Locate the entry in the parent directory and mark it deleted. */
    uint32_t parent_cluster;
    const char* filename;
    rc = fat32_resolve_parent_cluster(fs, path, &parent_cluster, &filename);
    if (rc != 0) return rc;

    char name83[8], ext83[3];
    rc = fat32_name_to_83(filename, name83, ext83);
    if (rc != 0) return rc;

    uint8_t* cluster_buf = kmalloc(fs->bytes_per_cluster);
    if (!cluster_buf) return -ENOMEM;

    uint32_t cluster = parent_cluster;
    size_t max_iterations = fs->total_clusters + 1;
    bool found = false;

    while (!found && cluster >= 2 && !fat32_is_eoc(cluster) &&
           !fat32_is_bad(cluster) && max_iterations-- > 0) {
        rc = fat32_read_cluster(fs, cluster, cluster_buf);
        if (rc != 0) { kfree(cluster_buf); return rc; }

        size_t epc = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);

        for (size_t i = 0; i < epc; i++) {
            fat32_dir_entry_t* de = (fat32_dir_entry_t*)(cluster_buf +
                                    i * sizeof(fat32_dir_entry_t));
            uint8_t first = (uint8_t)de->name[0];

            if (first == FAT_ENTRY_END) goto unlink_scan_done;
            if (first == FAT_ENTRY_FREE) continue;
            if ((de->attr & FAT_ATTR_LONG_NAME_MASK) == FAT_ATTR_LONG_NAME) continue;
            if (de->attr & FAT_ATTR_DIRECTORY) continue;

            if (memcmp(de->name, name83, 8) == 0 && memcmp(de->ext, ext83, 3) == 0) {
                de->name[0] = (char)FAT_ENTRY_FREE;
                rc = fat32_write_cluster(fs, cluster, cluster_buf);
                if (rc != 0) { kfree(cluster_buf); return rc; }
                found = true;
                break;
            }
        }

        if (!found) {
            uint32_t next;
            rc = fat32_read_fat_entry(fs, cluster, &next);
            if (rc != 0) { kfree(cluster_buf); return rc; }
            cluster = next;
        }
    }

unlink_scan_done:
    kfree(cluster_buf);

    if (!found) return -ENOENT;

    /* Free all clusters belonging to the deleted file. */
    if (entry.first_cluster >= 2) {
        rc = fat32_free_chain(fs, entry.first_cluster);
        if (rc != 0) return rc;
    }

    LOG_DEBUG("fat32_vfs_unlink: deleted file '%s'\n", path);
    return 0;
}

static const vfs_fs_ops_t fat32_ops = {
    .name    = "fat32",
    .mount   = fat32_mount,
    .unmount = fat32_unmount,
    .open    = fat32_vfs_open,
    .close   = fat32_vfs_close,
    .read    = fat32_vfs_read,
    .write   = fat32_vfs_write,
    .seek    = fat32_vfs_seek,
    .mkdir   = fat32_vfs_mkdir,
    .rmdir   = fat32_vfs_rmdir,
    .readdir = fat32_vfs_readdir,
    .stat    = fat32_vfs_stat,
    .unlink  = fat32_vfs_unlink,
    .ioctl   = NULL,
    .sync    = NULL,
};
