#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_FILES 256
#define MAX_HANDLES 64
#define MAX_FILENAME 256
#define MAX_FILE_SIZE 65536

#define VFS_READ   0x01
#define VFS_WRITE  0x02
#define VFS_CREAT  0x04
#define VFS_APPEND 0x08

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

#define DEV_NULL_FD   1000
#define DEV_ZERO_FD   1001
#define DEV_FULL_FD   1002
#define DEV_STDIN_FD  1003
#define DEV_STDOUT_FD 1004
#define DEV_STDERR_FD 1005

#define ENOSPC  28
#define EACCES  13
#define ENOTTY  25
#define EBADF   9
#define ENOSYS  38

typedef enum {
    VFS_TYPE_FILE,
    VFS_TYPE_DIR,
    VFS_TYPE_DEVICE
} vfs_file_type_t;

typedef struct vfs_file_t vfs_file_t;

typedef long vfs_off_t;
typedef long vfs_ssize_t;

typedef vfs_ssize_t (*vfs_dev_read_t)(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos);
typedef vfs_ssize_t (*vfs_dev_write_t)(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos);
typedef vfs_off_t (*vfs_dev_seek_t)(vfs_file_t* file, vfs_off_t offset, int whence, vfs_off_t* pos);
typedef int (*vfs_dev_ioctl_t)(vfs_file_t* file, unsigned long request, void* arg);

typedef struct {
    vfs_dev_read_t read;
    vfs_dev_write_t write;
    vfs_dev_seek_t seek;
    vfs_dev_ioctl_t ioctl;
} vfs_device_ops_t;

struct vfs_file_t {
    char name[MAX_FILENAME];
    bool used;
    size_t size;
    vfs_file_type_t type;
    char data[MAX_FILE_SIZE];

    vfs_device_ops_t ops;
    void* dev_data;
};

typedef struct {
    bool used;
    int fd;
    vfs_file_t* file;
    vfs_off_t position;
    int flags;
} vfs_handle_t;

void vfs_init(void);
int vfs_mkdir(const char* dirname);
int vfs_create(const char* filename, const char* data, size_t size);
int vfs_pseudo_register(const char* filename, vfs_dev_read_t read_fn, vfs_dev_write_t write_fn,
              vfs_dev_seek_t seek_fn, vfs_dev_ioctl_t ioctl_fn, void* dev_data);
const char* vfs_read(const char* filename, size_t* size);
int vfs_open(const char* filename, int flags);
vfs_ssize_t vfs_readfd(int fd, void* buf, size_t count);
vfs_ssize_t vfs_writefd(int fd, const void* buf, size_t count);
int vfs_close(int fd);
vfs_off_t vfs_seek(int fd, vfs_off_t offset, int whence);
int vfs_delete(const char* filename);
int vfs_ioctl(int fd, unsigned long request, void* arg);
bool vfs_exists(const char* filename);
bool vfs_is_dir(const char* path);
bool vfs_is_device(const char* path);
int vfs_count(void);
vfs_file_t* vfs_get_files(void);
void vfs_list_dir(const char* dirname);
void vfs_list(void);
int vfs_pseudo_register_with_fd(const char* filename, int fixed_fd,
                            vfs_dev_read_t read_fn,
                            vfs_dev_write_t write_fn,
                            vfs_dev_seek_t seek_fn,
                            vfs_dev_ioctl_t ioctl_fn,
                            void* dev_data);
void vfs_link_std_fd(int std_fd, const char* dev_name);

#endif