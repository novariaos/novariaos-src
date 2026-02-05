 // SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/vfs.h>
#include <core/fs/devfs.h>
#include <core/fs/procfs.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>
#include <core/kernel/mem.h>
#include <stddef.h>
#include <string.h>

#define DEV_STDIN_FD 1003
#define DEV_STDOUT_FD 1004
#define DEV_STDERR_FD 1005

static vfs_file_t files[MAX_FILES];
static vfs_handle_t handles[MAX_HANDLES];
static int next_fd = 3;

static vfs_filesystem_t registered_fs[MAX_REGISTERED_FS];
static vfs_mount_t mounts[MAX_MOUNTS];

/* Helper to find handle by FD /
static vfs_handle_t get_handle(int fd) {
if (fd < 0) return NULL;
for (int i = 0; i < MAX_HANDLES; i++) {
if (handles[i].used && handles[i].fd == fd) {
return &handles[i];
}
}
return NULL;
}

/* Internal FD allocator */
static int allocate_fd(void) {
int reserved_fds[] = {
DEV_NULL_FD, DEV_ZERO_FD, DEV_FULL_FD,
DEV_STDIN_FD, DEV_STDOUT_FD, DEV_STDERR_FD
};
int num_reserved = 6;
plaintext

for (int i = next_fd; i < MAX_HANDLES + 3; i++) {
    int is_reserved = 0;
    for (int r = 0; r < num_reserved; r++) {
        if (i == reserved_fds[r]) {
            is_reserved = 1;
            break;
        }
    }
    if (is_reserved) continue;
    
    int found = 0;
    for (int j = 0; j < MAX_HANDLES; j++) {
        if (handles[j].used && handles[j].fd == i) {
            found = 1;
            break;
        }
    }
    if (!found) {
        next_fd = i + 1;
        if (next_fd >= MAX_HANDLES + 3) next_fd = 3;
        return i;
    }
}

for (int i = 3; i < MAX_HANDLES + 3; i++) {
    int is_reserved = 0;
    for (int r = 0; r < num_reserved; r++) {
        if (i == reserved_fds[r]) {
            is_reserved = 1;
            break;
        }
    }
    if (is_reserved) continue;
    
    int found = 0;
    for (int j = 0; j < MAX_HANDLES; j++) {
        if (handles[j].used && handles[j].fd == i) {
            found = 1;
            break;
        }
    }
    if (!found) {
        next_fd = i + 1;
        return i;
    }
}

return -1;

}

int vfs_pseudo_register_with_fd(const char* filename, int fixed_fd,
vfs_dev_read_t read_fn,
vfs_dev_write_t write_fn,
vfs_dev_seek_t seek_fn,
vfs_dev_ioctl_t ioctl_fn,
void* dev_data) {
if (strlen(filename) >= MAX_FILENAME) {
return -1;
}
plaintext

for (int i = 0; i < MAX_HANDLES; i++) {
    if (handles[i].used && handles[i].fd == fixed_fd) {
        return -5;
    }
}

vfs_file_t* file = NULL;
int file_idx = -1;

for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && strcmp(files[i].name, filename) == 0) {
        file_idx = i;
        file = &files[i];
        break;
    }
}

if (file_idx == -1) {
    file_idx = vfs_pseudo_register(filename, read_fn, write_fn, seek_fn, ioctl_fn, dev_data);
    if (file_idx < 0) return file_idx;
    file = &files[file_idx];
}

int handle_idx = -1;
for (int i = 0; i < MAX_HANDLES; i++) {
    if (!handles[i].used) {
        handle_idx = i;
        break;
    }
}

if (handle_idx == -1) return -2;

handles[handle_idx].used = true;
handles[handle_idx].fd = fixed_fd;
handles[handle_idx].file = file;
handles[handle_idx].mount = NULL;
handles[handle_idx].private_data = NULL;
handles[handle_idx].position = 0;
handles[handle_idx].flags = VFS_READ | VFS_WRITE;

if (strcmp(filename, "/dev/stdout") == 0 ||
    strcmp(filename, "/dev/stderr") == 0) {
    handles[handle_idx].flags = VFS_WRITE;
}
else if (strcmp(filename, "/dev/stdin") == 0) {
    handles[handle_idx].flags = VFS_READ;
}

return fixed_fd;

}

void vfs_link_std_fd(int std_fd, const char* dev_name) {
if (std_fd < 0 || std_fd >= 3) return;
plaintext

vfs_file_t* dev_file = NULL;
for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && strcmp(files[i].name, dev_name) == 0) {
        dev_file = &files[i];
        break;
    }
}

if (dev_file) {
    handles[std_fd].file = dev_file;
    handles[std_fd].mount = NULL;
    handles[std_fd].used = true;
    handles[std_fd].fd = std_fd;
}

}

void vfs_init(void) {
// Initialize legacy file table
for (int i = 0; i < MAX_FILES; i++) {
memset(&files[i], 0, sizeof(vfs_file_t));
files[i].used = false;
files[i].type = VFS_TYPE_FILE;
}
plaintext

// Initialize handles
for (int i = 0; i < MAX_HANDLES; i++) {
    memset(&handles[i], 0, sizeof(vfs_handle_t));
    handles[i].used = false;
    handles[i].fd = -1;
}

// Initialize new VFS tables
for (int i = 0; i < MAX_REGISTERED_FS; i++) {
    registered_fs[i].registered = false;
    registered_fs[i].ops = NULL;
    registered_fs[i].name[0] = '\0';
}

for (int i = 0; i < MAX_MOUNTS; i++) {
    mounts[i].mounted = false;
    mounts[i].fs = NULL;
    mounts[i].fs_private = NULL;
    mounts[i].ref_count = 0;
}

// Standard file descriptors
handles[0].used = true;
handles[0].fd = 0;
handles[0].flags = VFS_READ;

handles[1].used = true;
handles[1].fd = 1;
handles[1].flags = VFS_WRITE;

handles[2].used = true;
handles[2].fd = 2;
handles[2].flags = VFS_WRITE;

// Create base directories
vfs_mkdir("/home");
vfs_mkdir("/tmp");
vfs_mkdir("/var");
vfs_mkdir("/var/log");
vfs_mkdir("/var/cache");
vfs_mkdir("/dev");

// Initialize filesystems
devfs_init();
procfs_init();

}

int vfs_mkdir(const char* dirname) {
if (!dirname || strlen(dirname) >= MAX_FILENAME) return -EINVAL;
plaintext

for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && strcmp(files[i].name, dirname) == 0) {
        return (files[i].type == VFS_TYPE_DIR) ? i : -EEXIST;
    }
}

for (int i = 0; i < MAX_FILES; i++) {
    if (!files[i].used) {
        strncpy(files[i].name, dirname, MAX_FILENAME - 1);
        files[i].name[MAX_FILENAME - 1] = '\0';
        files[i].size = 0;
        files[i].used = true;
        files[i].type = VFS_TYPE_DIR;
        return i;
    }
}

return -ENOMEM;

}

int vfs_create(const char* filename, const char* data, size_t size) {
if (!filename || strlen(filename) >= MAX_FILENAME) return -EINVAL;
if (size > MAX_FILE_SIZE) return -EFBIG;
plaintext

for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && strcmp(files[i].name, filename) == 0) {
        if (files[i].type == VFS_TYPE_DIR) return -EISDIR;
        memcpy(files[i].data, data, size);
        files[i].size = size;
        return i;
    }
}

for (int i = 0; i < MAX_FILES; i++) {
    if (!files[i].used) {
        strncpy(files[i].name, filename, MAX_FILENAME - 1);
        files[i].name[MAX_FILENAME - 1] = '\0';
        memcpy(files[i].data, data, size);
        files[i].size = size;
        files[i].used = true;
        files[i].type = VFS_TYPE_FILE;
        return i;
    }
}

return -ENOMEM;

}

int vfs_pseudo_register(const char* filename,
vfs_dev_read_t read_fn,
vfs_dev_write_t write_fn,
vfs_dev_seek_t seek_fn,
vfs_dev_ioctl_t ioctl_fn,
void* dev_data) {
if (!filename || strlen(filename) >= MAX_FILENAME) return -EINVAL;
plaintext

for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && strcmp(files[i].name, filename) == 0) {
        if (files[i].type == VFS_TYPE_DIR) return -EISDIR;
        files[i].ops.read = read_fn;
        files[i].ops.write = write_fn;
        files[i].ops.seek = seek_fn;
        files[i].ops.ioctl = ioctl_fn;
        files[i].dev_data = dev_data;
        files[i].type = VFS_TYPE_DEVICE;
        return i;
    }
}

for (int i = 0; i < MAX_FILES; i++) {
    if (!files[i].used) {
        strncpy(files[i].name, filename, MAX_FILENAME - 1);
        files[i].name[MAX_FILENAME - 1] = '\0';
        files[i].size = 0;
        files[i].used = true;
        files[i].type = VFS_TYPE_DEVICE;
        files[i].ops.read = read_fn;
        files[i].ops.write = write_fn;
        files[i].ops.seek = seek_fn;
        files[i].ops.ioctl = ioctl_fn;
        files[i].dev_data = dev_data;
        return i;
    }
}

return -ENOMEM;

}

const char* vfs_read(const char* filename, size_t* size) {
vfs_file_t* file = NULL;
for (int i = 0; i < MAX_FILES; i++) {
if (files[i].used && strcmp(files[i].name, filename) == 0) {
file = &files[i];
break;
}
}
plaintext

if (!file || file->type == VFS_TYPE_DEVICE) {
    if (size) *size = 0;
    return NULL;
}

if (size) *size = file->size;
return file->data;

}

int vfs_open(const char* filename, int flags) {
const char* rel_path = NULL;
vfs_mount_t* mnt = vfs_find_mount(filename, &rel_path);
vfs_file_t* legacy_file = NULL;
void* fs_private = NULL;
plaintext

// 1. Check if it's in a mounted filesystem
if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->open) {
    int res = mnt->fs->ops->open(mnt, rel_path, flags, &fs_private);
    if (res < 0) return res;
} else {
    // 2. Check legacy files
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {
            legacy_file = &files[i];
            break;
        }
    }
    
    if (!legacy_file && (flags & VFS_CREAT)) {
        int idx = vfs_create(filename, "", 0);
        if (idx >= 0) legacy_file = &files[idx];
    }
    
    if (!legacy_file) return -ENOENT;
}

// 3. Allocate handle and FD
int handle_idx = -1;
for (int i = 0; i < MAX_HANDLES; i++) {
    if (!handles[i].used) {
        handle_idx = i;
        break;
    }
}

if (handle_idx == -1) {
    // Cleanup if it was a mount open
    if (mnt && mnt->fs->ops->close) {
        vfs_handle_t tmp_h = { .mount = mnt, .private_data = fs_private };
        mnt->fs->ops->close(&tmp_h);
    }
    return -ENOMEM;
}

int fd = allocate_fd();
if (fd == -1) {
    if (mnt && mnt->fs->ops->close) {
        vfs_handle_t tmp_h = { .mount = mnt, .private_data = fs_private };
        mnt->fs->ops->close(&tmp_h);
    }
    return -EMFILE;
}

handles[handle_idx].used = true;
handles[handle_idx].fd = fd;
handles[handle_idx].file = legacy_file;
handles[handle_idx].mount = mnt;
handles[handle_idx].private_data = fs_private;
handles[handle_idx].position = 0;
handles[handle_idx].flags = flags;

if (mnt) mnt->ref_count++;

return fd;

}

vfs_ssize_t vfs_readfd(int fd, void* buf, size_t count) {
vfs_handle_t* handle = get_handle(fd);
if (!handle) return -EBADF;
if (!(handle->flags & VFS_READ)) return -EACCES;
plaintext

// Check mounted FS
if (handle->mount && handle->mount->fs->ops->read) {
    return handle->mount->fs->ops->read(handle, buf, count);
}

vfs_file_t* file = handle->file;
if (!file) {
    // Fallback for linked stdio if not explicitly pointing to a file
    if (fd == 0 || fd == DEV_STDIN_FD) return 0;
    return -EBADF;
}

if (file->type == VFS_TYPE_DEVICE) {
    if (!file->ops.read) return -EACCES;
    return file->ops.read(file, buf, count, &handle->position);
}

if (handle->position >= (vfs_off_t)file->size) return 0;

size_t remaining = file->size - handle->position;
size_t to_read = count < remaining ? count : remaining;

memcpy(buf, file->data + handle->position, to_read);
handle->position += to_read;

return (vfs_ssize_t)to_read;

}

vfs_ssize_t vfs_writefd(int fd, const void* buf, size_t count) {
vfs_handle_t* handle = get_handle(fd);
if (!handle) return -EBADF;
if (!(handle->flags & VFS_WRITE)) return -EACCES;
plaintext

// Check mounted FS
if (handle->mount && handle->mount->fs->ops->write) {
    return handle->mount->fs->ops->write(handle, buf, count);
}

vfs_file_t* file = handle->file;
if (!file) {
    if (fd == 1 || fd == 2 || fd == DEV_STDOUT_FD || fd == DEV_STDERR_FD) return (vfs_ssize_t)count;
    return -EBADF;
}

if (file->type == VFS_TYPE_DEVICE) {
    if (!file->ops.write) return -EACCES;
    return file->ops.write(file, buf, count, &handle->position);
}

if (handle->position + (vfs_off_t)count > MAX_FILE_SIZE) {
    count = MAX_FILE_SIZE - handle->position;
}

if (count == 0 && MAX_FILE_SIZE > 0) return -ENOSPC;

memcpy(file->data + handle->position, buf, count);
handle->position += count;

if (handle->position > (vfs_off_t)file->size) {
    file->size = (size_t)handle->position;
}

return (vfs_ssize_t)count;

}

int vfs_close(int fd) {
if (fd >= 0 && fd < 3) return 0; // Don't close stdio by default here
plaintext

for (int i = 0; i < MAX_HANDLES; i++) {
    if (handles[i].used && handles[i].fd == fd) {
        if (handles[i].mount) {
            if (handles[i].mount->fs->ops->close) {
                handles[i].mount->fs->ops->close(&handles[i]);
            }
            handles[i].mount->ref_count--;
        }
        handles[i].used = false;
        handles[i].fd = -1;
        handles[i].file = NULL;
        handles[i].mount = NULL;
        handles[i].private_data = NULL;
        return 0;
    }
}

return -EBADF;

}

vfs_off_t vfs_seek(int fd, vfs_off_t offset, int whence) {
vfs_handle_t* handle = get_handle(fd);
if (!handle) return -EBADF;
plaintext

if (handle->mount && handle->mount->fs->ops->lseek) {
    return handle->mount->fs->ops->lseek(handle, offset, whence);
}

vfs_file_t* file = handle->file;
if (!file) return -EBADF;

if (file->type == VFS_TYPE_DEVICE && file->ops.seek) {
    return file->ops.seek(file, offset, whence, &handle->position);
}

vfs_off_t new_pos;
switch (whence) {
    case VFS_SEEK_SET: new_pos = offset; break;
    case VFS_SEEK_CUR: new_pos = handle->position + offset; break;
    case VFS_SEEK_END: new_pos = (vfs_off_t)file->size + offset; break;
    default: return -EINVAL;
}

if (new_pos < 0) new_pos = 0;
if (file->type != VFS_TYPE_DEVICE && new_pos > (vfs_off_t)file->size) {
    new_pos = (vfs_off_t)file->size;
}

handle->position = new_pos;
return new_pos;

}

int vfs_delete(const char* filename) {
for (int i = 0; i < MAX_FILES; i++) {
if (files[i].used && strcmp(files[i].name, filename) == 0) {
// Check if file is open
for (int j = 0; j < MAX_HANDLES; j++) {
if (handles[j].used && handles[j].file == &files[i]) {
return -EBUSY;
}
}
plaintext

memset(&files[i], 0, sizeof(vfs_file_t));
        files[i].used = false;
        return 0;
    }
}
return -ENOENT;

}

int vfs_rmdir(const char* dirname) {
if (!dirname || strcmp(dirname, "/") == 0) return -EBUSY;
plaintext

vfs_file_t* dir = NULL;
int dir_idx = -1;

for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && strcmp(files[i].name, dirname) == 0) {
        if (files[i].type != VFS_TYPE_DIR) return -ENOTDIR;
        dir = &files[i];
        dir_idx = i;
        break;
    }
}

if (!dir) return -ENOENT;

// Check if directory is empty (legacy check)
size_t dir_len = strlen(dirname);
for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && i != dir_idx) {
        if (strncmp(files[i].name, dirname, dir_len) == 0 && 
            files[i].name[dir_len] == '/') {
            return -ENOTEMPTY;
        }
    }
}

dir->used = false;
return 0;

}

int vfs_ioctl(int fd, unsigned long request, void* arg) {
vfs_handle_t* handle = get_handle(fd);
if (!handle) return -EBADF;
plaintext

if (handle->mount && handle->mount->fs->ops->ioctl) {
    return handle->mount->fs->ops->ioctl(handle, request, arg);
}

vfs_file_t* file = handle->file;
if (file && file->type == VFS_TYPE_DEVICE && file->ops.ioctl) {
    return file->ops.ioctl(file, request, arg);
}

return -ENOTTY;

}

bool vfs_exists(const char* filename) {
if (vfs_find_mount(filename, NULL)) return true;
for (int i = 0; i < MAX_FILES; i++) {
if (files[i].used && strcmp(files[i].name, filename) == 0) return true;
}
return false;
}

bool vfs_is_dir(const char* path) {
vfs_stat_t st;
if (vfs_stat(path, &st) == 0) return (st.st_mode & VFS_S_IFDIR) != 0;
return false;
}

bool vfs_is_device(const char* path) {
for (int i = 0; i < MAX_FILES; i++) {
if (files[i].used && strcmp(files[i].name, path) == 0) {
return files[i].type == VFS_TYPE_DEVICE;
}
}
return false;
}

int vfs_count(void) {
int count = 0;
for (int i = 0; i < MAX_FILES; i++) {
if (files[i].used) count++;
}
return count;
}

vfs_file_t* vfs_get_files(void) {
return files;
}

void vfs_list_dir(const char* dirname) {
vfs_dirent_t entries[32];
int count = vfs_readdir(dirname, entries, 32);
if (count < 0) return;
plaintext

LOG_DEBUG("Listing directory: %s\n", dirname);
for (int i = 0; i < count; i++) {
    LOG_DEBUG("  %s [%d]\n", entries[i].d_name, entries[i].d_type);
}

}

void vfs_list(void) {
LOG_DEBUG("VFS Legacy Contents:\n");
for (int i = 0; i < MAX_FILES; i++) {
if (files[i].used) {
LOG_DEBUG(" [%d] %s (%u bytes, type=%d)\n", i, files[i].name, (unsigned int)files[i].size, files[i].type);
}
}
LOG_DEBUG("Mount Points:\n");
for (int i = 0; i < MAX_MOUNTS; i++) {
if (mounts[i].mounted) {
LOG_DEBUG(" %s on %s (refs: %d)\n", mounts[i].fs->name, mounts[i].mount_point, mounts[i].ref_count);
}
}
}


// new vfs abstraction layer implementation

int vfs_register_filesystem(const char* name, const vfs_fs_ops_t* ops, uint32_t flags) {
if (!name || !ops) return -EINVAL;
if (strlen(name) >= MAX_FS_NAME) return -EINVAL;
plaintext

for (int i = 0; i < MAX_REGISTERED_FS; i++) {
    if (registered_fs[i].registered && strcmp(registered_fs[i].name, name) == 0) {
        return -EEXIST;
    }
}

for (int i = 0; i < MAX_REGISTERED_FS; i++) {
    if (!registered_fs[i].registered) {
        strncpy(registered_fs[i].name, name, MAX_FS_NAME - 1);
        registered_fs[i].name[MAX_FS_NAME - 1] = '\0';
        registered_fs[i].ops = ops;
        registered_fs[i].flags = flags;
        registered_fs[i].registered = true;
        return 0;
    }
}

return -ENOMEM;

}

int vfs_unregister_filesystem(const char* name) {
if (!name) return -EINVAL;
plaintext

for (int i = 0; i < MAX_REGISTERED_FS; i++) {
    if (registered_fs[i].registered && strcmp(registered_fs[i].name, name) == 0) {
        for (int j = 0; j < MAX_MOUNTS; j++) {
            if (mounts[j].mounted && mounts[j].fs == &registered_fs[i]) {
                return -EBUSY;
            }
        }
        registered_fs[i].registered = false;
        registered_fs[i].ops = NULL;
        return 0;
    }
}

return -ENOENT;

}

vfs_filesystem_t* vfs_find_filesystem(const char* name) {
if (!name) return NULL;
plaintext

for (int i = 0; i < MAX_REGISTERED_FS; i++) {
    if (registered_fs[i].registered && strcmp(registered_fs[i].name, name) == 0) {
        return &registered_fs[i];
    }
}

return NULL;

}

int vfs_mount_fs(const char* fs_name, const char* mount_point,
const char* device, uint32_t flags, void* data) {
if (!fs_name || !mount_point) return -EINVAL;
plaintext

vfs_filesystem_t* fs = vfs_find_filesystem(fs_name);
if (!fs) return -ENODEV;

for (int i = 0; i < MAX_MOUNTS; i++) {
    if (mounts[i].mounted && strcmp(mounts[i].mount_point, mount_point) == 0) {
        return -EBUSY;
    }
}

vfs_mount_t* mnt = NULL;
for (int i = 0; i < MAX_MOUNTS; i++) {
    if (!mounts[i].mounted) {
        mnt = &mounts[i];
        break;
    }
}

if (!mnt) return -ENOMEM;

strncpy(mnt->mount_point, mount_point, MAX_MOUNT_PATH - 1);
mnt->mount_point[MAX_MOUNT_PATH - 1] = '\0';

if (device) {
    strncpy(mnt->device, device, MAX_FILENAME - 1);
    mnt->device[MAX_FILENAME - 1] = '\0';
} else {
    mnt->device[0] = '\0';
}

mnt->fs = fs;
mnt->flags = flags;
mnt->ref_count = 0;
mnt->fs_private = NULL;

if (fs->ops && fs->ops->mount) {
    int result = fs->ops->mount(mnt, device, data);
    if (result < 0) return result;
}

mnt->mounted = true;
return 0;

}

int vfs_umount(const char* mount_point) {
if (!mount_point) return -EINVAL;
plaintext

for (int i = 0; i < MAX_MOUNTS; i++) {
    if (mounts[i].mounted && strcmp(mounts[i].mount_point, mount_point) == 0) {
        if (mounts[i].ref_count > 0) return -EBUSY;

        if (mounts[i].fs->ops && mounts[i].fs->ops->unmount) {
            mounts[i].fs->ops->unmount(&mounts[i]);
        }

        mounts[i].mounted = false;
        mounts[i].fs = NULL;
        mounts[i].fs_private = NULL;
        return 0;
    }
}

return -ENOENT;

}

vfs_mount_t* vfs_find_mount(const char* path, const char** relative_path) {
if (!path) return NULL;
plaintext

vfs_mount_t* best_match = NULL;
size_t best_len = 0;

for (int i = 0; i < MAX_MOUNTS; i++) {
    if (!mounts[i].mounted) continue;
    size_t mnt_len = strlen(mounts[i].mount_point);

    // Longest prefix match
    if (strncmp(path, mounts[i].mount_point, mnt_len) == 0) {
        // Check if it's a directory boundary
        if (path[mnt_len] == '/' || path[mnt_len] == '\0' || (mnt_len == 1 && mounts[i].mount_point[0] == '/')) {
            if (mnt_len >= best_len) {
                best_match = &mounts[i];
                best_len = mnt_len;
            }
        }
    }
}

if (best_match && relative_path) {
    *relative_path = path + best_len;
    if (**relative_path == '/') (*relative_path)++;
}

return best_match;

}

int vfs_stat(const char* path, vfs_stat_t* stat) {
if (!path || !stat) return -EINVAL;
const char* rel_path;
vfs_mount_t* mnt = vfs_find_mount(path, &rel_path);
plaintext

if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->stat) {
    return mnt->fs->ops->stat(mnt, rel_path, stat);
}

for (int i = 0; i < MAX_FILES; i++) {
    if (files[i].used && strcmp(files[i].name, path) == 0) {
        stat->st_size = files[i].size;
        stat->st_mode = (files[i].type == VFS_TYPE_DIR) ? VFS_S_IFDIR : VFS_S_IFREG;
        if (files[i].type == VFS_TYPE_DEVICE) stat->st_mode = VFS_S_IFCHR;
        return 0;
    }
}
return -ENOENT;

}

int vfs_readdir(const char* path, vfs_dirent_t* entries, size_t max_entries) {
if (!path || !entries) return -EINVAL;
const char* rel_path;
vfs_mount_t* mnt = vfs_find_mount(path, &rel_path);
plaintext

if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->readdir) {
    return mnt->fs->ops->readdir(mnt, rel_path, entries, max_entries);
}

int count = 0;
size_t path_len = strlen(path);
// Ensure path ends with / for easier prefix matching if not root
char search_path[MAX_FILENAME];
strncpy(search_path, path, MAX_FILENAME - 2);
if (path_len > 0 && search_path[path_len - 1] != '/' && strcmp(path, "/") != 0) {
    search_path[path_len] = '/';
    search_path[path_len + 1] = '\0';
    path_len++;
} else {
    search_path[path_len] = '\0';
}

for (int i = 0; i < MAX_FILES && (size_t)count < max_entries; i++) {
    if (!files[i].used) continue;
    
    const char* name = files[i].name;
    if (strcmp(path, "/") == 0) {
        // Root special case: find files starting with / but having no other /
        if (name[0] == '/' && name[1] != '\0' && strchr(name + 1, '/') == NULL) {
            strncpy(entries[count].d_name, name + 1, 255);
            entries[count].d_type = files[i].type;
            count++;
        }
    } else if (strncmp(name, search_path, path_len) == 0) {
        const char* sub = name + path_len;
        if (*sub != '\0' && strchr(sub, '/') == NULL) {
            strncpy(entries[count].d_name, sub, 255);
            entries[count].d_type = files[i].type;
            count++;
        }
    }
}
return count;

}
