// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/vfs.h>
#include <core/fs/devfs.h>
#include <core/fs/procfs.h>
#include <core/kernel/kstd.h>
#include <log.h>
#include <core/kernel/mem.h>
#include <stddef.h>
#include <string.h>

#define DEV_STDIN_FD  1003
#define DEV_STDOUT_FD 1004
#define DEV_STDERR_FD 1005

#ifndef VFS_TRUNC
#define VFS_TRUNC 0x04
#endif

static vfs_file_t files[MAX_FILES];
static vfs_handle_t handles[MAX_HANDLES];
static int next_fd = 3;

static vfs_filesystem_t registered_fs[MAX_REGISTERED_FS];
static vfs_mount_t mounts[MAX_MOUNTS];
static vfs_file_handle_t file_handles[MAX_HANDLES];

static vfs_handle_t* get_handle(int fd) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].used && handles[i].fd == fd) {
            return &handles[i];
        }
    }
    return NULL;
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
    vfs_file_t* dev_file = NULL;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, dev_name) == 0) {
            dev_file = &files[i];
            break;
        }
    }
    
    if (!dev_file) return;
    
    if (std_fd >= 0 && std_fd < 3) {
        handles[std_fd].file = dev_file;
    }
}

static int allocate_fd(void) {
    int reserved_fds[] = {
        DEV_NULL_FD, DEV_ZERO_FD, DEV_FULL_FD, 
        DEV_STDIN_FD, DEV_STDOUT_FD, DEV_STDERR_FD
    };
    int num_reserved = 6;
    
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

void vfs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        files[i].used = false;
        files[i].size = 0;
        files[i].name[0] = '\0';
        files[i].data[0] = '\0';
        files[i].type = VFS_TYPE_FILE;
        files[i].ops.read = NULL;
        files[i].ops.write = NULL;
        files[i].ops.seek = NULL;
        files[i].ops.ioctl = NULL;
        files[i].dev_data = NULL;
    }

    for (int i = 0; i < MAX_HANDLES; i++) {
        handles[i].used = false;
        handles[i].fd = -1;
        handles[i].file = NULL;
        handles[i].position = 0;
        handles[i].flags = 0;
    }

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

    for (int i = 0; i < MAX_HANDLES; i++) {
        file_handles[i].used = false;
        file_handles[i].fd = -1;
        file_handles[i].mount = NULL;
        file_handles[i].private_data = NULL;
    }

    handles[0].used = true;
    handles[0].fd = 0;
    handles[0].flags = VFS_READ;

    handles[1].used = true;
    handles[1].fd = 1;
    handles[1].flags = VFS_WRITE;

    handles[2].used = true;
    handles[2].fd = 2;
    handles[2].flags = VFS_WRITE;

    devfs_init();
    procfs_init();
}

int vfs_mkdir(const char* dirname) {
    if (strlen(dirname) >= MAX_FILENAME) {
        return -1;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, dirname) == 0) {
            if (files[i].type == VFS_TYPE_DIR) {
                return i;
            }
            return -2;
        }
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            strcpy(files[i].name, dirname);
            files[i].size = 0;
            files[i].used = true;
            files[i].type = VFS_TYPE_DIR;
            return i;
        }
    }
    
    return -3;
}

int vfs_create(const char* filename, const char* data, size_t size) {
    if (strlen(filename) >= MAX_FILENAME) return -1;
    if (size > MAX_FILE_SIZE) return -2;

    int fd = vfs_open(filename, VFS_CREAT | VFS_WRITE | VFS_TRUNC);
    if (fd < 0) return -3;

    vfs_ssize_t written = vfs_writefd(fd, data, size);
    vfs_close(fd);

    if (written != (vfs_ssize_t)size) return -4;

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {
            return i;
        }
    }
    return -5;
}

int vfs_pseudo_register(const char* filename, 
            vfs_dev_read_t read_fn, 
            vfs_dev_write_t write_fn,
            vfs_dev_seek_t seek_fn,
            vfs_dev_ioctl_t ioctl_fn,
            void* dev_data) {
    if (strlen(filename) >= MAX_FILENAME) {
        return -1;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {
            if (files[i].type == VFS_TYPE_DIR) {
                return -4;
            }
            files[i].ops.read = read_fn;
            files[i].ops.write = write_fn;
            files[i].ops.seek = seek_fn;
            files[i].ops.ioctl = ioctl_fn;
            files[i].dev_data = dev_data;
            return i;
        }
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            strcpy(files[i].name, filename);
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
    
    return -3;
}

const char* vfs_read(const char* filename, size_t* size) {
    static char buffer[MAX_FILE_SIZE];

    int fd = vfs_open(filename, VFS_READ);
    if (fd < 0) {
        if (size) *size = 0;
        return NULL;
    }

    vfs_ssize_t bytes = vfs_readfd(fd, buffer, MAX_FILE_SIZE - 1);
    vfs_close(fd);

    if (bytes < 0) {
        if (size) *size = 0;
        return NULL;
    }

    buffer[bytes] = '\0';
    if (size) *size = bytes;
    return buffer;
}

int vfs_open(const char* filename, int flags) {
    vfs_file_t* file = NULL;
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {
            file = &files[i];
            break;
        }
    }
    
    if (!file) {
        const char* rel_path;
        vfs_mount_t* mnt = vfs_find_mount(filename, &rel_path);
        if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->stat) {
            vfs_stat_t stat;
            if (mnt->fs->ops->stat(mnt, rel_path, &stat) == 0) {
                for (int i = 0; i < MAX_FILES; i++) {
                    if (!files[i].used) {
                        strcpy(files[i].name, filename);
                        files[i].size = stat.st_size;
                        files[i].used = true;
                        files[i].type = VFS_TYPE_FILE;
                        files[i].mount = (void*)mnt;
                        files[i].fs_private = NULL;
                        file = &files[i];
                        break;
                    }
                }
            }
        }
    }
    
    if (!file && (flags & VFS_CREAT)) {
        for (int i = 0; i < MAX_FILES; i++) {
            if (!files[i].used) {
                strcpy(files[i].name, filename);
                files[i].size = 0;
                files[i].used = true;
                files[i].type = VFS_TYPE_FILE;
                file = &files[i];
                break;
            }
        }
    }
    
    if (!file) return -1;
    
    int handle_idx = -1;
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (!handles[i].used) {
            handle_idx = i;
            break;
        }
    }
    
    if (handle_idx == -1) return -2;
    
    int fd = allocate_fd();
    if (fd == -1) return -3;
    
    handles[handle_idx].used = true;
    handles[handle_idx].fd = fd;
    handles[handle_idx].file = file;
    handles[handle_idx].position = 0;
    handles[handle_idx].flags = flags;
    handles[handle_idx].fs_private = file->fs_private;
    handles[handle_idx].mount = file->mount;
    
    return fd;
}

vfs_ssize_t vfs_readfd(int fd, void* buf, size_t count) {
    vfs_handle_t* handle = get_handle(fd);
    if (!handle) return -EBADF;
    if (!(handle->flags & VFS_READ)) return -EACCES;
    
    vfs_file_t* file = handle->file;
    if (!file) return -EBADF;
    
    if (fd == 0 || fd == DEV_STDIN_FD) {
        return 0;
    }
    
    if (file->type == VFS_TYPE_DEVICE) {
        if (!file->ops.read) return -EACCES;
        return file->ops.read(file, buf, count, &handle->position);
    }
    
    if (file->mount) {
        vfs_mount_t* mnt = (vfs_mount_t*)file->mount;
        if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->read) {
            vfs_file_handle_t* fh = NULL;
            for (int i = 0; i < MAX_HANDLES; i++) {
                if (file_handles[i].used && file_handles[i].fd == fd) {
                    fh = &file_handles[i];
                    break;
                }
            }
            
            if (!fh) {
                for (int i = 0; i < MAX_HANDLES; i++) {
                    if (!file_handles[i].used) {
                        fh = &file_handles[i];
                        fh->used = true;
                        fh->fd = fd;
                        fh->mount = mnt;
                        fh->position = handle->position;
                        fh->flags = handle->flags;
                        
                        const char* rel_path;
                        vfs_find_mount(file->name, &rel_path);
                        strcpy(fh->path, rel_path);
                        
                        if (mnt->fs->ops->open) {
                            int ret = mnt->fs->ops->open(mnt, rel_path, handle->flags, fh);
                            if (ret < 0) {
                                fh->used = false;
                                return ret;
                            }
                        }
                        break;
                    }
                }
            }
            
            if (fh) {
                fh->position = handle->position;
                vfs_ssize_t result = mnt->fs->ops->read(mnt, fh, buf, count);
                if (result > 0) {
                    handle->position += result;
                }
                return result;
            }
        }
        return -EIO;
    }
    
    if (handle->position >= file->size) return 0;
    
    size_t remaining = file->size - handle->position;
    size_t to_read = count < remaining ? count : remaining;
    
    memcpy(buf, file->data + handle->position, to_read);
    handle->position += to_read;
    
    return to_read;
}

vfs_ssize_t vfs_writefd(int fd, const void* buf, size_t count) {
    vfs_handle_t* handle = get_handle(fd);
    if (!handle) return -EBADF;
    if (!(handle->flags & VFS_WRITE)) return -EACCES;
    
    vfs_file_t* file = handle->file;
    
    if (fd == 1 || fd == 2 || fd == DEV_STDOUT_FD || fd == DEV_STDERR_FD) {
        return count;
    }
    
    if (!file) return -EBADF;
    
    if (file->type == VFS_TYPE_DEVICE) {
        if (!file->ops.write) {
            return -EACCES;
        }
        return file->ops.write(file, buf, count, &handle->position);
    }
    
    if (handle->position + count > MAX_FILE_SIZE) {
        count = MAX_FILE_SIZE - handle->position;
    }
    
    if (count == 0) return -ENOSPC;
    
    memcpy(file->data + handle->position, buf, count);
    handle->position += count;
    
    if (handle->position > file->size) {
        file->size = handle->position;
    }
    
    return count;
}

int vfs_close(int fd) {
    if (fd < 3) return 0;
    
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].used && handles[i].fd == fd) {
            handles[i].used = false;
            handles[i].fd = -1;
            handles[i].file = NULL;
            handles[i].position = 0;
            handles[i].flags = 0;
            return 0;
        }
    }
    
    return -1;
}

vfs_off_t vfs_seek(int fd, vfs_off_t offset, int whence) {
    vfs_handle_t* handle = get_handle(fd);
    if (!handle) return -1;
    
    vfs_file_t* file = handle->file;
    if (!file) return -2;
    
    if (file->type == VFS_TYPE_DEVICE && file->ops.seek) {
        return file->ops.seek(file, offset, whence, &handle->position);
    }
    
    vfs_off_t new_pos;
    
    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = handle->position + offset;
            break;
        case VFS_SEEK_END:
            new_pos = file->size + offset;
            break;
        default:
            return -3;
    }
    
    if (new_pos < 0) new_pos = 0;
    if (new_pos > file->size) new_pos = file->size;
    
    handle->position = new_pos;
    return new_pos;
}

int vfs_delete(const char* filename) {
    const char* rel_path;
    vfs_mount_t* mnt = vfs_find_mount(filename, &rel_path);
    if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->unlink) {
        return mnt->fs->ops->unlink(mnt, rel_path);
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {

            for (int j = 0; j < MAX_HANDLES; j++) {
                if (handles[j].used && handles[j].file == &files[i]) {
                    handles[j].used = false;
                    handles[j].fd = -1;
                    handles[j].file = NULL;
                }
            }

            files[i].used = false;
            files[i].size = 0;
            files[i].name[0] = '\0';
            files[i].data[0] = '\0';
            files[i].type = VFS_TYPE_FILE;
            files[i].ops.read = NULL;
            files[i].ops.write = NULL;
            files[i].ops.seek = NULL;
            files[i].ops.ioctl = NULL;
            files[i].dev_data = NULL;
            return 0;
        }
    }

    return -ENOENT;
}

int vfs_rmdir(const char* dirname) {
    if (strlen(dirname) >= MAX_FILENAME) {
        return -1;
    }

    vfs_file_t* dir = NULL;
    int dir_idx = -1;
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, dirname) == 0) {
            if (files[i].type != VFS_TYPE_DIR) {
                return -ENOTDIR;
            }
            dir = &files[i];
            dir_idx = i;
            break;
        }
    }
    
    if (!dir) {
        return -ENOENT; 
    }

    if (strcmp(dirname, "/") == 0) {
        return -EBUSY;
    }

    int dir_len = strlen(dirname);
    char normalized_dir[MAX_FILENAME];
    strcpy(normalized_dir, dirname);

    if (normalized_dir[dir_len - 1] != '/') {
        normalized_dir[dir_len] = '/';
        normalized_dir[dir_len + 1] = '\0';
        dir_len++;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && i != dir_idx) {
            const char* name = files[i].name;
            int name_len = strlen(name);

            if (name_len > dir_len && 
                strncmp(name, normalized_dir, dir_len) == 0) {
                
                for (int j = 0; j < MAX_HANDLES; j++) {
                    if (handles[j].used && handles[j].file == &files[i]) {
                        handles[j].used = false;
                        handles[j].fd = -1;
                        handles[j].file = NULL;
                        handles[j].position = 0;
                        handles[j].flags = 0;
                    }
                }
                
                files[i].used = false;
                files[i].size = 0;
                files[i].name[0] = '\0';
                files[i].data[0] = '\0';
                files[i].type = VFS_TYPE_FILE;
                files[i].ops.read = NULL;
                files[i].ops.write = NULL;
                files[i].ops.seek = NULL;
                files[i].ops.ioctl = NULL;
                files[i].dev_data = NULL;
            }
        }
    }

    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].used && handles[i].file == dir) {
            handles[i].used = false;
            handles[i].fd = -1;
            handles[i].file = NULL;
            handles[i].position = 0;
            handles[i].flags = 0;
        }
    }
    
    files[dir_idx].used = false;
    files[dir_idx].size = 0;
    files[dir_idx].name[0] = '\0';
    files[dir_idx].data[0] = '\0';
    files[dir_idx].type = VFS_TYPE_FILE;
    files[dir_idx].ops.read = NULL;
    files[dir_idx].ops.write = NULL;
    files[dir_idx].ops.seek = NULL;
    files[dir_idx].ops.ioctl = NULL;
    files[dir_idx].dev_data = NULL;
    
    return 0;
}

int vfs_ioctl(int fd, unsigned long request, void* arg) {
    vfs_handle_t* handle = get_handle(fd);
    if (!handle) return -1;
    
    vfs_file_t* file = handle->file;
    if (!file) return -2;
    
    if (file->type == VFS_TYPE_DEVICE && file->ops.ioctl) {
        return file->ops.ioctl(file, request, arg);
    }
    
    return -ENOTTY;
}

bool vfs_exists(const char* filename) {
        for (int i = 0; i < MAX_FILES; i++) {
            if (files[i].used && strcmp(files[i].name, filename) == 0) {
                return true;
            }
        }
        
        const char* rel_path;
        vfs_mount_t* mnt = vfs_find_mount(filename, &rel_path);
        if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->stat) {
            vfs_stat_t stat;
            if (mnt->fs->ops->stat(mnt, rel_path, &stat) == 0) {
                return true;
            }
        }
        
        return false;
}

bool vfs_is_dir(const char* path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, path) == 0) {
            return files[i].type == VFS_TYPE_DIR;
        }
    }
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
        if (files[i].used) {
            count++;
        }
    }
    return count;
}

vfs_file_t* vfs_get_files(void) {
    return files;
}

void vfs_list_dir(const char* dirname) {
    int dir_len = strlen(dirname);
    
    char normalized_dir[MAX_FILENAME];
    strcpy(normalized_dir, dirname);
    if (dir_len > 1 && normalized_dir[dir_len - 1] == '/') {
        normalized_dir[dir_len - 1] = '\0';
        dir_len--;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) continue;
        
        const char* name = files[i].name;
        bool should_show = false;
        
        if (dir_len == 1 && normalized_dir[0] == '/') {
            if (name[0] == '/' && name[1] != '\0') {
                int slash_count = 0;
                for (int j = 1; name[j] != '\0'; j++) {
                    if (name[j] == '/') slash_count++;
                }
                if (slash_count == 0) {
                    should_show = true;
                }
            }
        } else {
            int name_len = strlen(name);
            if (name_len > dir_len && 
                name[dir_len] == '/' &&
                strncmp(name, normalized_dir, dir_len) == 0) {
                int slash_count = 0;
                for (int j = dir_len + 1; name[j] != '\0'; j++) {
                    if (name[j] == '/') slash_count++;
                }
                if (slash_count == 0) {
                    should_show = true;
                }
            }
        }
    }
}

void vfs_list(void) {
    int count = 0;

    LOG_DEBUG("VFS Contents:\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            count++;
            LOG_DEBUG("  [%d] %s (%d bytes, type=%d)\n", i, files[i].name, files[i].size, files[i].type);
        }
    }
    LOG_DEBUG("Total files: %d\n", count);
}

int vfs_register_filesystem(const char* name, const vfs_fs_ops_t* ops, uint32_t flags) {
    if (!name || !ops) return -EINVAL;
    if (strlen(name) >= MAX_FS_NAME) return -EINVAL;

    for (int i = 0; i < MAX_REGISTERED_FS; i++) {
        if (registered_fs[i].registered && strcmp(registered_fs[i].name, name) == 0) {
            return -EEXIST;
        }
    }

    for (int i = 0; i < MAX_REGISTERED_FS; i++) {
        if (!registered_fs[i].registered) {
            strcpy(registered_fs[i].name, name);
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
    if (strlen(mount_point) >= MAX_MOUNT_PATH) return -EINVAL;

    int mkdir_result = vfs_mkdir(mount_point);
    if (mkdir_result < 0 && mkdir_result != -EEXIST) {
        return mkdir_result;
    }

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

    strcpy(mnt->mount_point, mount_point);
    if (device) {
        strcpy(mnt->device, device);
    } else {
        mnt->device[0] = '\0';
    }
    mnt->fs = fs;
    mnt->flags = flags;
    mnt->ref_count = 0;
    mnt->fs_private = NULL;

    if (fs->ops && fs->ops->mount) {
        int result = fs->ops->mount(mnt, device, data);
        if (result < 0) {
            mnt->mounted = false;
            return result;
        }
    }

    mnt->mounted = true;
    return 0;
}

int vfs_umount(const char* mount_point) {
    if (!mount_point) return -EINVAL;

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts[i].mounted && strcmp(mounts[i].mount_point, mount_point) == 0) {
            vfs_mount_t* mnt = &mounts[i];

            if (mnt->ref_count > 0) {
                return -EBUSY;
            }

            if (mnt->fs && mnt->fs->ops && mnt->fs->ops->unmount) {
                int result = mnt->fs->ops->unmount(mnt);
                if (result < 0) {
                    return result;
                }
            }

            mnt->mounted = false;
            mnt->fs = NULL;
            mnt->fs_private = NULL;
            return 0;
        }
    }

    return -ENOENT;
}

vfs_mount_t* vfs_find_mount(const char* path, const char** relative_path) {
    if (!path) return NULL;

    vfs_mount_t* best_match = NULL;
    size_t best_len = 0;

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts[i].mounted) continue;

        size_t mount_len = strlen(mounts[i].mount_point);

        if (strncmp(path, mounts[i].mount_point, mount_len) == 0) {
            if (path[mount_len] == '/' || path[mount_len] == '\0' ||
                (mount_len == 1 && mounts[i].mount_point[0] == '/')) {
                if (mount_len > best_len) {
                    best_match = &mounts[i];
                    best_len = mount_len;
                }
            }
        }
    }

    if (best_match && relative_path) {
        *relative_path = path + best_len;
        if (**relative_path == '/') (*relative_path)++;
        if (**relative_path == '\0') *relative_path = "";
    }

    return best_match;
}

int vfs_stat(const char* path, vfs_stat_t* stat) {
    if (!path || !stat) return -EINVAL;

    const char* rel_path;
    vfs_mount_t* mnt = vfs_find_mount(path, &rel_path);

    if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->stat) {
        return mnt->fs->ops->stat(mnt, rel_path, stat);
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, path) == 0) {
            stat->st_size = files[i].size;
            stat->st_blksize = 512;
            stat->st_mtime = 0;

            if (files[i].type == VFS_TYPE_DIR) {
                stat->st_mode = VFS_S_IFDIR | 0755;
            } else if (files[i].type == VFS_TYPE_DEVICE) {
                stat->st_mode = VFS_S_IFCHR | 0666;
            } else {
                stat->st_mode = VFS_S_IFREG | 0644;
            }
            return 0;
        }
    }

    return -ENOENT;
}

int vfs_readdir(const char* path, vfs_dirent_t* entries, size_t max_entries) {
    if (!path || !entries || max_entries == 0) return -EINVAL;

    const char* rel_path;
    vfs_mount_t* mnt = vfs_find_mount(path, &rel_path);
    int count = 0;

    if (mnt && mnt->fs && mnt->fs->ops && mnt->fs->ops->readdir) {
        int fs_count = mnt->fs->ops->readdir(mnt, rel_path, entries, max_entries);
        if (fs_count > 0) {
            count = fs_count;
        }
    } 
    else {
        size_t path_len = strlen(path);

        char normalized_path[MAX_FILENAME];
        strcpy(normalized_path, path);
        if (path_len > 1 && normalized_path[path_len - 1] == '/') {
            normalized_path[path_len - 1] = '\0';
            path_len--;
        }

        for (int i = 0; i < MAX_FILES && (size_t)count < max_entries; i++) {
            if (!files[i].used) continue;

            const char* name = files[i].name;
            size_t name_len = strlen(name);

            bool is_child = false;

            if (path_len == 1 && normalized_path[0] == '/') {
                if (name[0] == '/' && name_len > 1) {
                    int slashes = 0;
                    for (size_t j = 1; j < name_len; j++) {
                        if (name[j] == '/') slashes++;
                    }
                    is_child = (slashes == 0);
                }
            } else {
                if (name_len > path_len && name[path_len] == '/' &&
                    strncmp(name, normalized_path, path_len) == 0) {
                    int slashes = 0;
                    for (size_t j = path_len + 1; j < name_len; j++) {
                        if (name[j] == '/') slashes++;
                    }
                    is_child = (slashes == 0);
                }
            }

            if (is_child) {
                const char* basename = name;
                if (path_len == 1 && normalized_path[0] == '/') {
                    basename = name + 1;
                } else {
                    basename = name + path_len + 1;
                }

                strcpy(entries[count].d_name, basename);
                entries[count].d_type = files[i].type;
                count++;
            }
        }
    }

    if (count < (int)max_entries) {
        size_t path_len = strlen(path);

        for (int i = 0; i < MAX_MOUNTS; i++) {
            if (!mounts[i].mounted) continue;
            
            const char* mount_point = mounts[i].mount_point;
            size_t mount_len = strlen(mount_point);
            
            bool is_child = false;
            const char* basename = NULL;
            
            if (path_len == 1 && path[0] == '/') {
                if (mount_point[0] == '/' && mount_len > 1) {
                    int slash_pos = 1;
                    while (slash_pos < (int)mount_len && mount_point[slash_pos] != '/') {
                        slash_pos++;
                    }
                    
                    if (slash_pos == (int)mount_len || mount_point[slash_pos] == '/') {
                        is_child = true;
                        static char first_component[MAX_FILENAME];
                        int j;
                        for (j = 1; j < slash_pos && j < MAX_FILENAME-1; j++) {
                            first_component[j-1] = mount_point[j];
                        }
                        first_component[j-1] = '\0';
                        basename = first_component;
                    }
                }
            } else {
                if (mount_len > path_len && strncmp(mount_point, path, path_len) == 0) {
                    if (mount_point[path_len] == '/') {
                        int start = path_len + 1;
                        int end = start;
                        while (end < (int)mount_len && mount_point[end] != '/') {
                            end++;
                        }

                        if (end == (int)mount_len || mount_point[end] == '/') {
                            is_child = true;
                            static char component[MAX_FILENAME];
                            int j;
                            for (j = start; j < end && j-start < MAX_FILENAME-1; j++) {
                                component[j-start] = mount_point[j];
                            }
                            component[j-start] = '\0';
                            basename = component;
                        }
                    }
                }
            }
            
            if (is_child && basename) {
                bool already_exists = false;
                for (int j = 0; j < count; j++) {
                    if (strcmp(entries[j].d_name, basename) == 0) {
                        already_exists = true;
                        break;
                    }
                }
                
                if (!already_exists && count < (int)max_entries) {
                    strcpy(entries[count].d_name, basename);
                    entries[count].d_type = VFS_TYPE_DIR;
                    count++;
                }
            }
        }
    }

    return count;
}