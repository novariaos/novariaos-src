// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/iso9660.h>
#include <core/fs/vfs.h>
#include <core/kernel/kstd.h>
#include <core/kernel/vge/fb_render.h>
#include <core/kernel/mem.h>
#include <string.h>
#include <errno.h>

static uint8_t* iso_data = NULL;
static size_t iso_data_size = 0;
static bool initialized = false;

typedef struct iso9660_mount {
    uint8_t* data;
    size_t size;
    uint16_t block_size;
    iso9660_pvd_t pvd;
    int ref_count;
} iso9660_mount_t;

typedef struct iso9660_file {
    uint32_t extent;
    uint32_t size;
    uint32_t position;
    uint32_t flags;
    iso9660_mount_t* fs;
} iso9660_file_t;

static const uint8_t* iso_read_block(iso9660_mount_t* fs, uint32_t lba) {
    if (!fs || (size_t)lba * fs->block_size >= fs->size) {
        return NULL;
    }
    return fs->data + lba * fs->block_size;
}

static void normalize_filename(const char* iso_name, size_t iso_len, char* out, size_t out_size) {
    size_t i;
    for (i = 0; i < iso_len && i < out_size - 1; i++) {
        if (iso_name[i] == ';') break;
        out[i] = iso_name[i];
    }
    out[i] = '\0';
    
    size_t len = i;
    if (len > 0 && out[len - 1] == '.') {
        out[len - 1] = '\0';
    }
}

static void to_lowercase(char* str) {
    for (size_t i = 0; str[i]; i++) {
        if (str[i] >= 'A' && str[i] <= 'Z') {
            str[i] = str[i] + ('a' - 'A');
        }
    }
}

static iso9660_dir_entry_t* find_entry_in_dir(iso9660_mount_t* fs, uint32_t dir_extent, uint32_t dir_size, const char* name) {
    const uint8_t* dir_data = iso_read_block(fs, dir_extent);
    if (!dir_data) return NULL;

    size_t offset = 0;
    while (offset < dir_size) {
        iso9660_dir_entry_t* entry = (iso9660_dir_entry_t*)(dir_data + offset);
        if (entry->length == 0) break;

        char* entry_name = (char*)(entry + 1);
        uint8_t name_len = entry->name_len;

        if (name_len == 1 && (entry_name[0] == 0 || entry_name[0] == 1)) {
            offset += entry->length;
            continue;
        }

        char normalized[256];
        normalize_filename(entry_name, name_len, normalized, sizeof(normalized));
        to_lowercase(normalized);

        char name_lower[256];
        strcpy(name_lower, name);
        to_lowercase(name_lower);

        if (strcmp(normalized, name_lower) == 0) {
            return entry;
        }

        offset += entry->length;
    }

    return NULL;
}

// VFS Filesystem Operations
static int iso9660_mount(vfs_mount_t* mnt, const char* device, void* data) {
    (void)data;
    
    if (!device) return -EINVAL;
    
    if (!iso_data || !initialized) {
        return -ENODEV;
    }
    
    iso9660_mount_t* fs_data = (iso9660_mount_t*)kmalloc(sizeof(iso9660_mount_t));
    if (!fs_data) return -ENOMEM;
    
    fs_data->data = iso_data;
    fs_data->size = iso_data_size;
    fs_data->block_size = 2048;
    fs_data->ref_count = 1;

    for (int i = 16; i < 32; i++) {
        const uint8_t* block = iso_read_block(fs_data, i);
        if (!block) continue;
        
        iso9660_pvd_t* vd = (iso9660_pvd_t*)block;
        if (vd->type == 1 && strncmp(vd->identifier, "CD001", 5) == 0) {
            memcpy(&fs_data->pvd, vd, sizeof(iso9660_pvd_t));
            fs_data->block_size = vd->logical_block_size_le;
            break;
        }
    }
    
    mnt->fs_private = fs_data;
    return 0;
}

static int iso9660_unmount(vfs_mount_t* mnt) {
    if (!mnt || !mnt->fs_private) return -EINVAL;
    
    iso9660_mount_t* fs_data = (iso9660_mount_t*)mnt->fs_private;
    fs_data->ref_count--;
    
    if (fs_data->ref_count <= 0) {
        kfree(fs_data);
    }
    
    mnt->fs_private = NULL;
    return 0;
}

static int iso9660_open(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h) {
    if (!mnt || !path || !h) return -EINVAL;
    if (flags & VFS_WRITE) return -EROFS;
    
    iso9660_mount_t* fs_data = (iso9660_mount_t*)mnt->fs_private;
    if (!fs_data) return -EINVAL;

    iso9660_dir_entry_t* root = (iso9660_dir_entry_t*)fs_data->pvd.root_directory_entry;
    uint32_t current_extent = root->extent_le;
    uint32_t current_size = root->size_le;
    
    if (path[0] == '/') path++;
    
    if (path[0] == '\0') {
        iso9660_file_t* file_data = (iso9660_file_t*)kmalloc(sizeof(iso9660_file_t));
        if (!file_data) return -ENOMEM;
        
        file_data->extent = current_extent;
        file_data->size = current_size;
        file_data->position = 0;
        file_data->flags = flags;
        file_data->fs = fs_data;
        
        h->private_data = file_data;
        strcpy(h->path, path);
        return 0;
    }

    char path_copy[256];
    strcpy(path_copy, path);
    
    char* token = path_copy;
    while (*token) {
        char* next_slash = token;
        while (*next_slash && *next_slash != '/') next_slash++;
        
        char component[256];
        size_t comp_len = next_slash - token;
        if (comp_len >= sizeof(component)) return -ENAMETOOLONG;
        
        memcpy(component, token, comp_len);
        component[comp_len] = '\0';
        
        iso9660_dir_entry_t* entry = find_entry_in_dir(fs_data, current_extent, current_size, component);
        if (!entry) return -ENOENT;
        
        current_extent = entry->extent_le;
        current_size = entry->size_le;
        
        token = next_slash;
        if (*token == '/') token++;
    }

    iso9660_file_t* file_data = (iso9660_file_t*)kmalloc(sizeof(iso9660_file_t));
    if (!file_data) return -ENOMEM;
    
    file_data->extent = current_extent;
    file_data->size = current_size;
    file_data->position = 0;
    file_data->flags = flags;
    file_data->fs = fs_data;
    
    h->private_data = file_data;
    strcpy(h->path, path);
    return 0;
}

static int iso9660_close(vfs_mount_t* mnt, vfs_file_handle_t* h) {
    (void)mnt;
    
    if (!h || !h->private_data) return -EINVAL;
    
    kfree(h->private_data);
    h->private_data = NULL;
    return 0;
}

static vfs_ssize_t iso9660_read(vfs_mount_t* mnt, vfs_file_handle_t* h, void* buf, size_t count) {
    if (!mnt || !h || !buf) return -EINVAL;
    
    iso9660_file_t* file = (iso9660_file_t*)h->private_data;
    if (!file) return -EINVAL;
    
    iso9660_mount_t* fs_data = file->fs;
    if (!fs_data) return -EINVAL;
    
    if (file->position >= file->size) return 0;
    
    size_t remaining = file->size - file->position;
    size_t to_read = count < remaining ? count : remaining;
    
    uint32_t block = file->extent + (file->position / fs_data->block_size);
    size_t offset = file->position % fs_data->block_size;
    
    const uint8_t* data = iso_read_block(fs_data, block);
    if (!data) return -EIO;
    
    if (offset + to_read <= fs_data->block_size) {
        memcpy(buf, data + offset, to_read);
        file->position += to_read;
        h->position = file->position;
        return to_read;
    }
    
    size_t total_read = 0;
    uint8_t* buf_ptr = (uint8_t*)buf;
    
    while (to_read > 0) {
        data = iso_read_block(fs_data, block);
        if (!data) break;
        
        size_t chunk = fs_data->block_size - offset;
        if (chunk > to_read) chunk = to_read;
        
        memcpy(buf_ptr, data + offset, chunk);
        
        buf_ptr += chunk;
        file->position += chunk;
        total_read += chunk;
        to_read -= chunk;
        
        block++;
        offset = 0;
    }
    
    h->position = file->position;
    return total_read;
}

static vfs_ssize_t iso9660_write(vfs_mount_t* mnt, vfs_file_handle_t* h, const void* buf, size_t count) {
    (void)mnt; (void)h; (void)buf; (void)count;
    return -EROFS;
}

static vfs_off_t iso9660_seek(vfs_mount_t* mnt, vfs_file_handle_t* h, vfs_off_t offset, int whence) {
    (void)mnt;
    
    if (!h || !h->private_data) return -EINVAL;
    
    iso9660_file_t* file = (iso9660_file_t*)h->private_data;
    vfs_off_t new_pos;
    
    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = file->position + offset;
            break;
        case VFS_SEEK_END:
            new_pos = file->size + offset;
            break;
        default:
            return -EINVAL;
    }
    
    if (new_pos < 0) new_pos = 0;
    if (new_pos > file->size) new_pos = file->size;
    
    file->position = new_pos;
    h->position = new_pos;
    return new_pos;
}

static int iso9660_stat(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat) {
    if (!mnt || !path || !stat) return -EINVAL;
    
    iso9660_mount_t* fs_data = (iso9660_mount_t*)mnt->fs_private;
    if (!fs_data) return -EINVAL;

    iso9660_dir_entry_t* root = (iso9660_dir_entry_t*)fs_data->pvd.root_directory_entry;
    uint32_t current_extent = root->extent_le;
    uint32_t current_size = root->size_le;
    
    if (path[0] == '/') path++;
    
    if (path[0] == '\0') {
        stat->st_size = current_size;
        stat->st_mode = VFS_S_IFDIR | 0555;
        stat->st_blksize = fs_data->block_size;
        stat->st_mtime = 0;
        return 0;
    }

    char path_copy[256];
    strcpy(path_copy, path);
    
    char* token = path_copy;
    while (*token) {
        char* next_slash = token;
        while (*next_slash && *next_slash != '/') next_slash++;
        
        char component[256];
        size_t comp_len = next_slash - token;
        if (comp_len >= sizeof(component)) return -ENAMETOOLONG;
        
        memcpy(component, token, comp_len);
        component[comp_len] = '\0';
        
        iso9660_dir_entry_t* entry = find_entry_in_dir(fs_data, current_extent, current_size, component);
        if (!entry) return -ENOENT;
        
        bool is_last = (*next_slash == '\0');
        
        if (is_last) {
            stat->st_size = entry->size_le;
            stat->st_blksize = fs_data->block_size;
            stat->st_mtime = 0;
            
            if (entry->flags & ISO_FLAG_DIRECTORY) {
                stat->st_mode = VFS_S_IFDIR | 0555;
            } else {
                stat->st_mode = VFS_S_IFREG | 0444;
            }
            return 0;
        }
        
        current_extent = entry->extent_le;
        current_size = entry->size_le;
        
        token = next_slash;
        if (*token == '/') token++;
    }
    
    return -ENOENT;
}

static int iso9660_readdir(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max_entries) {
    if (!mnt || !path || !entries || max_entries == 0) return -EINVAL;
    
    iso9660_mount_t* fs_data = (iso9660_mount_t*)mnt->fs_private;
    if (!fs_data) return -EINVAL;

    vfs_stat_t st;
    int rc = iso9660_stat(mnt, path, &st);
    if (rc < 0) return rc;
    
    if (!(st.st_mode & VFS_S_IFDIR)) return -ENOTDIR;

    iso9660_dir_entry_t* root = (iso9660_dir_entry_t*)fs_data->pvd.root_directory_entry;
    uint32_t dir_extent = root->extent_le;
    uint32_t dir_size = root->size_le;
    
    if (path[0] != '\0' && strcmp(path, "/") != 0) {
        char path_copy[256];
        strcpy(path_copy, path);
        
        uint32_t current_extent = root->extent_le;
        uint32_t current_size = root->size_le;
        
        char* token = path_copy;
        if (token[0] == '/') token++;
        
        while (*token) {
            char* next_slash = token;
            while (*next_slash && *next_slash != '/') next_slash++;
            
            char component[256];
            size_t comp_len = next_slash - token;
            if (comp_len >= sizeof(component)) return -ENAMETOOLONG;
            
            memcpy(component, token, comp_len);
            component[comp_len] = '\0';
            
            iso9660_dir_entry_t* entry = find_entry_in_dir(fs_data, current_extent, current_size, component);
            if (!entry) return -ENOENT;
            
            current_extent = entry->extent_le;
            current_size = entry->size_le;
            
            token = next_slash;
            if (*token == '/') token++;
        }
        
        dir_extent = current_extent;
        dir_size = current_size;
    }

    const uint8_t* dir_data = iso_read_block(fs_data, dir_extent);
    if (!dir_data) return -EIO;
    
    size_t offset = 0;
    int count = 0;
    
    while (offset < dir_size && count < (int)max_entries) {
        iso9660_dir_entry_t* entry = (iso9660_dir_entry_t*)(dir_data + offset);
        if (entry->length == 0) break;
        
        char* entry_name = (char*)(entry + 1);
        uint8_t name_len = entry->name_len;
        
        if (name_len == 1 && (entry_name[0] == 0 || entry_name[0] == 1)) {
            offset += entry->length;
            continue;
        }
        
        char normalized[256];
        normalize_filename(entry_name, name_len, normalized, sizeof(normalized));
        to_lowercase(normalized);
        
        strcpy(entries[count].d_name, normalized);
        entries[count].d_type = (entry->flags & ISO_FLAG_DIRECTORY) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        
        count++;
        offset += entry->length;
    }
    
    return count;
}

// VFS operations table
static const vfs_fs_ops_t iso9660_ops = {
    .mount = iso9660_mount,
    .unmount = iso9660_unmount,
    .open = iso9660_open,
    .close = iso9660_close,
    .read = iso9660_read,
    .write = iso9660_write,
    .seek = iso9660_seek,
    .stat = iso9660_stat,
    .readdir = iso9660_readdir,
    .mkdir = NULL,
    .rmdir = NULL,
    .unlink = NULL,
    .ioctl = NULL,
    .sync = NULL
};

void iso9660_init(void* iso_start, size_t iso_size) {
    iso_data = (uint8_t*)iso_start;
    iso_data_size = iso_size;
    
    for (int i = 16; i < 32; i++) {
        if ((size_t)i * 2048 >= iso_size) break;
        
        const uint8_t* block = iso_data + i * 2048;
        iso9660_pvd_t* vd = (iso9660_pvd_t*)block;
        
        if (vd->type == 1 && strncmp(vd->identifier, "CD001", 5) == 0) {
            initialized = true;
            break;
        }
    }
    
    if (initialized) {
        vfs_register_filesystem("iso9660", &iso9660_ops, 0);
    }
}

bool iso9660_is_initialized(void) {
    return initialized;
}

int iso9660_mount_to_vfs(const char* mount_point, const char* iso_path) {
    (void)iso_path;
    
    if (!initialized || !iso_data) {
        return -ENODEV;
    }
    
    return vfs_mount_fs("iso9660", mount_point, "iso0", 0, NULL);
}