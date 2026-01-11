// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/log.h>
#include <core/kernel/mem.h>
#include <core/kernel/kstd.h>
#include <core/fs/procfs.h>
#include <core/fs/devfs.h>
#include <core/fs/vfs.h>

#define DEV_NULL_FD   1000
#define DEV_ZERO_FD   1001
#define DEV_FULL_FD   1002
#define DEV_STDIN_FD  1003
#define DEV_STDOUT_FD 1004
#define DEV_STDERR_FD 1005

static vfs_file_t files[MAX_FILES];
static vfs_handle_t handles[MAX_HANDLES];
static int next_fd = 3;

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
    
    handles[0].used = true;
    handles[0].fd = 0;
    handles[0].flags = VFS_READ;
    
    handles[1].used = true;
    handles[1].fd = 1;
    handles[1].flags = VFS_WRITE;
    
    handles[2].used = true;
    handles[2].fd = 2;
    handles[2].flags = VFS_WRITE;

    vfs_mkdir("/home");
    vfs_mkdir("/tmp");
    vfs_mkdir("/var");
    vfs_mkdir("/var/log");
    vfs_mkdir("/var/cache");
    vfs_mkdir("/dev");

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
    if (strlen(filename) >= MAX_FILENAME) {
        return -1;
    }
    
    if (size > MAX_FILE_SIZE) {
        return -2;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {
            if (files[i].type == VFS_TYPE_DIR) {
                return -4;
            }
            memcpy(files[i].data, data, size);
            files[i].size = size;
            return i;
        }
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            strcpy(files[i].name, filename);
            memcpy(files[i].data, data, size);
            files[i].size = size;
            files[i].used = true;
            files[i].type = VFS_TYPE_FILE;
            return i;
        }
    }
    
    return -3;
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
    vfs_file_t* file = NULL;
    int file_idx = -1;
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {
            file = &files[i];
            file_idx = i;
            break;
        }
    }
    
    if (!file) {
        if (size) *size = 0;
        return NULL;
    }
    
    if (file->type == VFS_TYPE_DEVICE) {
        static char dev_buffer[MAX_FILE_SIZE];
        if (file->ops.read) {
            vfs_off_t pos = 0;
            vfs_ssize_t bytes = file->ops.read(file, dev_buffer, 
                                              MAX_FILE_SIZE, &pos);
            if (bytes > 0) {
                if (size) *size = bytes;
                dev_buffer[bytes] = '\0';
                return dev_buffer;
            }
        }
        if (size) *size = 0;
        return NULL;
    }

    if (size) *size = file->size;
    return file->data;
}

int vfs_open(const char* filename, int flags) {
    vfs_file_t* file = NULL;
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, filename) == 0) {
            file = &files[i];
            break;
        }
    }
    
    if (!file && (flags & VFS_CREAT)) {
        int idx = vfs_create(filename, "", 0);
        if (idx < 0) return -1;
        file = &files[idx];
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
        if (!file->ops.read) {
            return -EACCES;
        }
        return file->ops.read(file, buf, count, &handle->position);
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
    
    return -1;
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