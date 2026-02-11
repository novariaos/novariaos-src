#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_FILES 1024
#define MAX_HANDLES 64
#define MAX_FILENAME 256
#define MAX_FILE_SIZE 134217728

#define MAX_FS_NAME 32
#define MAX_REGISTERED_FS 16
#define MAX_MOUNT_PATH 256
#define MAX_MOUNTS 32

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

// Error codes
#define ENOSPC  28
#define EACCES  13
#define ENOTTY  25
#define EBADF   9
#define ENOSYS  38
#define EBUSY   16
#define ENOENT  2
#define ENOTDIR 20
#define EROFS   30
#define ENOMEM  12
#define EINVAL  22
#define EEXIST  17
#define EPERM   1
#define ENODEV  19

// File type bits for st_mode
#define VFS_S_IFMT   0xF000
#define VFS_S_IFREG  0x8000
#define VFS_S_IFDIR  0x4000
#define VFS_S_IFCHR  0x2000
#define VFS_S_IFBLK  0x6000

// Filesystem flags
#define VFS_FS_READONLY  0x01
#define VFS_FS_NODEV     0x02
#define VFS_FS_VIRTUAL   0x04

// Mount flags
#define VFS_MNT_READONLY 0x01

typedef enum {
    VFS_TYPE_FILE,
    VFS_TYPE_DIR,
    VFS_TYPE_DEVICE
} vfs_file_type_t;

typedef struct vfs_file_t vfs_file_t;

typedef long vfs_off_t;
typedef long vfs_ssize_t;

// Forward declarations for new VFS abstraction
typedef struct vfs_mount vfs_mount_t;
typedef struct vfs_file_handle vfs_file_handle_t;
typedef struct vfs_fs_ops vfs_fs_ops_t;
typedef struct vfs_filesystem vfs_filesystem_t;

// File statistics structure
typedef struct vfs_stat {
    uint32_t    st_mode;      // File type and permissions
    vfs_off_t   st_size;      // Size in bytes
    uint32_t    st_blksize;   // Block size for I/O
    uint64_t    st_mtime;     // Modification time
} vfs_stat_t;

// Directory entry structure
typedef struct vfs_dirent {
    char        d_name[MAX_FILENAME];
    uint32_t    d_type;       // VFS_TYPE_FILE/DIR/DEVICE
} vfs_dirent_t;

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
    char* data;
    bool owns_data;

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

// Filesystem operations interface - the key abstraction layer
struct vfs_fs_ops {
    const char* name;

    // Mount/unmount operations
    int (*mount)(vfs_mount_t* mnt, const char* device, void* data);
    int (*unmount)(vfs_mount_t* mnt);

    // File operations
    int (*open)(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h);
    int (*close)(vfs_mount_t* mnt, vfs_file_handle_t* h);
    vfs_ssize_t (*read)(vfs_mount_t* mnt, vfs_file_handle_t* h, void* buf, size_t count);
    vfs_ssize_t (*write)(vfs_mount_t* mnt, vfs_file_handle_t* h, const void* buf, size_t count);
    vfs_off_t (*seek)(vfs_mount_t* mnt, vfs_file_handle_t* h, vfs_off_t offset, int whence);

    // Directory operations
    int (*mkdir)(vfs_mount_t* mnt, const char* path, uint32_t mode);
    int (*rmdir)(vfs_mount_t* mnt, const char* path);
    int (*readdir)(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max_entries);

    // Metadata operations
    int (*stat)(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat);
    int (*unlink)(vfs_mount_t* mnt, const char* path);

    // Optional operations
    int (*ioctl)(vfs_mount_t* mnt, vfs_file_handle_t* h, unsigned long req, void* arg);
    int (*sync)(vfs_mount_t* mnt);
};

// Registered filesystem driver
struct vfs_filesystem {
    char name[MAX_FS_NAME];
    const vfs_fs_ops_t* ops;
    uint32_t flags;
    bool registered;
};

// Mount point
struct vfs_mount {
    char mount_point[MAX_MOUNT_PATH];
    char device[MAX_MOUNT_PATH];
    vfs_filesystem_t* fs;
    void* fs_private;        // Private data for the FS instance
    uint32_t flags;
    bool mounted;
    int ref_count;
};

// Extended file handle for new VFS
struct vfs_file_handle {
    bool used;
    int fd;
    vfs_mount_t* mount;
    char path[MAX_FILENAME];  // Path relative to mount point
    vfs_off_t position;
    int flags;
    void* private_data;       // For FS driver use
};

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
int vfs_rmdir(const char* dirname);
bool vfs_exists(const char* filename);
bool vfs_is_dir(const char* path);
bool vfs_is_device(const char* path);
int vfs_count(void);
vfs_file_t* vfs_get_files(void);
void vfs_list_dir(const char* dirname);
void vfs_list(void);
int vfs_create_static(const char* filename, const char* data, size_t size);
int vfs_pseudo_register_with_fd(const char* filename, int fixed_fd,
                            vfs_dev_read_t read_fn,
                            vfs_dev_write_t write_fn,
                            vfs_dev_seek_t seek_fn,
                            vfs_dev_ioctl_t ioctl_fn,
                            void* dev_data);
void vfs_link_std_fd(int std_fd, const char* dev_name);

// New VFS abstraction layer API
int vfs_register_filesystem(const char* name, const vfs_fs_ops_t* ops, uint32_t flags);
int vfs_unregister_filesystem(const char* name);
vfs_filesystem_t* vfs_find_filesystem(const char* name);

int vfs_mount_fs(const char* fs_name, const char* mount_point,
                 const char* device, uint32_t flags, void* data);
int vfs_umount(const char* mount_point);
vfs_mount_t* vfs_find_mount(const char* path, const char** relative_path);

int vfs_stat(const char* path, vfs_stat_t* stat);
int vfs_readdir(const char* path, vfs_dirent_t* entries, size_t max_entries);

#endif