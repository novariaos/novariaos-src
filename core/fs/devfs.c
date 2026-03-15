// SPDX-License-Identifier: GPL-3.0-only

#include <core/crypto/chacha20_rng.h>
#include <core/arch/entropy.h>
#include <core/kernel/kstd.h>
#include <core/kernel/mem.h>
#include <core/kernel/tty.h>
#include <core/fs/vfs.h>
#include <stdbool.h>
#include <string.h>

#define MAX_DEVICES 32

typedef struct {
    char name[64];
    struct {
        vfs_dev_read_t read;
        vfs_dev_write_t write;
        vfs_dev_seek_t seek;
        vfs_dev_ioctl_t ioctl;
    } ops;
    void* dev_data;
    bool used;
} devfs_device_t;

static devfs_device_t devices[MAX_DEVICES];

static devfs_device_t* devfs_find_device(const char* name) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].used && strcmp(devices[i].name, name) == 0) {
            return &devices[i];
        }
    }
    return NULL;
}

// File handle structure for opened devices
typedef struct {
    devfs_device_t* dev;
    vfs_off_t pos;
} devfs_handle_t;

// DevFS operations
static int devfs_mount(vfs_mount_t* mnt, const char* device, void* data) {
    (void)device; (void)data;
    mnt->fs_private = NULL;
    return 0;
}

static int devfs_unmount(vfs_mount_t* mnt) {
    (void)mnt;
    return 0;
}

static int devfs_open(vfs_mount_t* mnt, const char* path, int flags, vfs_file_handle_t* h) {
    (void)mnt; (void)flags;
    
    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        return -EISDIR;
    }
    
    devfs_device_t* dev = devfs_find_device(path);
    if (!dev) {
        return -ENOENT;
    }
    
    devfs_handle_t* priv = (devfs_handle_t*)kmalloc(sizeof(devfs_handle_t));
    if (!priv) {
        return -ENOMEM;
    }
    
    priv->dev = dev;
    priv->pos = 0;
    
    h->private_data = priv;
    return 0;
}

static int devfs_close(vfs_mount_t* mnt, vfs_file_handle_t* h) {
    (void)mnt;
    if (h->private_data) {
        kfree(h->private_data);
    }
    return 0;
}

static vfs_ssize_t devfs_read(vfs_mount_t* mnt, vfs_file_handle_t* h, void* buf, size_t count) {
    (void)mnt;
    devfs_handle_t* priv = (devfs_handle_t*)h->private_data;
    
    if (!priv || !priv->dev || !priv->dev->ops.read) {
        return -EACCES;
    }
    
    return priv->dev->ops.read(NULL, buf, count, &priv->pos);
}

static vfs_ssize_t devfs_write(vfs_mount_t* mnt, vfs_file_handle_t* h, const void* buf, size_t count) {
    (void)mnt;
    devfs_handle_t* priv = (devfs_handle_t*)h->private_data;
    
    if (!priv || !priv->dev || !priv->dev->ops.write) {
        return -EACCES;
    }
    
    return priv->dev->ops.write(NULL, buf, count, &priv->pos);
}

static vfs_off_t devfs_seek(vfs_mount_t* mnt, vfs_file_handle_t* h, vfs_off_t offset, int whence) {
    (void)mnt;
    devfs_handle_t* priv = (devfs_handle_t*)h->private_data;
    
    if (!priv || !priv->dev) {
        return -EBADF;
    }
    
    if (priv->dev->ops.seek) {
        return priv->dev->ops.seek(NULL, offset, whence, &priv->pos);
    }
    
    vfs_off_t new_pos;
    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = priv->pos + offset;
            break;
        case VFS_SEEK_END:
            new_pos = 0; // Devices have no size
            break;
        default:
            return -EINVAL;
    }
    
    if (new_pos < 0) new_pos = 0;
    priv->pos = new_pos;
    return new_pos;
}

static int devfs_ioctl(vfs_mount_t* mnt, vfs_file_handle_t* h, unsigned long req, void* arg) {
    (void)mnt;
    devfs_handle_t* priv = (devfs_handle_t*)h->private_data;
    
    if (!priv || !priv->dev) {
        return -EBADF;
    }
    
    if (priv->dev->ops.ioctl) {
        return priv->dev->ops.ioctl(NULL, req, arg);
    }
    
    return -ENOTTY;
}

static int devfs_stat(vfs_mount_t* mnt, const char* path, vfs_stat_t* stat) {
    (void)mnt;

    if (path[0] == '\0' || strcmp(path, "/") == 0) {
        stat->st_mode = VFS_S_IFDIR | 0755;
        stat->st_size = 0;
        stat->st_blksize = 512;
        stat->st_mtime = 0;
        return 0;
    }

    devfs_device_t* dev = devfs_find_device(path);
    if (dev) {
        stat->st_mode = VFS_S_IFCHR | 0666;
        stat->st_size = 0;
        stat->st_blksize = 512;
        stat->st_mtime = 0;
        return 0;
    }

    return -ENOENT;
}

static int devfs_readdir(vfs_mount_t* mnt, const char* path, vfs_dirent_t* entries, size_t max) {
    (void)mnt; (void)path;

    int count = 0;
    for (int i = 0; i < MAX_DEVICES && (size_t)count < max; i++) {
        if (devices[i].used) {
            strcpy(entries[count].d_name, devices[i].name);
            entries[count].d_type = VFS_TYPE_DEVICE;
            count++;
        }
    }
    return count;
}

static const vfs_fs_ops_t devfs_ops = {
    .name = "devfs",
    .mount = devfs_mount,
    .unmount = devfs_unmount,
    .open = devfs_open,
    .close = devfs_close,
    .read = devfs_read,
    .write = devfs_write,
    .seek = devfs_seek,
    .ioctl = devfs_ioctl,
    .stat = devfs_stat,
    .readdir = devfs_readdir,
    .mkdir = NULL,
    .rmdir = NULL,
    .unlink = NULL,
    .sync = NULL,
};

// Legacy device operation wrappers (keep these for vfs_pseudo_register compatibility)
static vfs_ssize_t dev_null_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)buf; (void)count; (void)pos;
    return 0;
}

static vfs_ssize_t dev_null_write(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)buf; (void)pos;
    return count;
}

static vfs_ssize_t dev_zero_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)pos;
    memset(buf, 0, count);
    return count;
}

static vfs_ssize_t dev_zero_write(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)buf; (void)pos;
    return count;
}

static vfs_ssize_t dev_full_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)pos;
    memset(buf, 0, count);
    return count;
}

static vfs_ssize_t dev_full_write(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)buf; (void)pos;
    return -ENOSPC;
}

static vfs_ssize_t dev_urandom_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)pos;
    static struct chacha20_rng rng;
    static int initialized = 0;
    
    if (!initialized) {
        uint64_t seed = get_hw_entropy();
        chacha20_rng_init(&rng, seed);
        initialized = 1;
    }
    
    size_t bytes_to_read = (count > 1024) ? 1024 : count;
    
    chacha20_rng_bytes(&rng, buf, bytes_to_read);
    
    return bytes_to_read;
}

static vfs_ssize_t dev_urandom_write(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)buf; (void)count; (void)pos;
    return -EACCES;
}

static vfs_off_t dev_null_seek(vfs_file_t* file, vfs_off_t offset, int whence, vfs_off_t* pos) {
    (void)file; (void)offset; (void)whence;
    *pos = 0;
    return 0;
}

static vfs_ssize_t dev_tty_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)buf; (void)count; (void)pos;
    return 1;
}

static vfs_ssize_t dev_tty_write(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)pos;
    tty_puts((const char*)buf);
    return count;
}

void devfs_register_device(const char* name, vfs_dev_read_t read_fn,
                           vfs_dev_write_t write_fn, void* data) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].used) {
            strcpy(devices[i].name, name);
            devices[i].ops.read = read_fn;
            devices[i].ops.write = write_fn;
            devices[i].ops.seek = NULL;
            devices[i].ops.ioctl = NULL;
            devices[i].dev_data = data;
            devices[i].used = true;
            return;
        }
    }
}

void devfs_init(void) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        devices[i].used = false;
    }

    // Register devfs filesystem
    vfs_register_filesystem("devfs", &devfs_ops, VFS_FS_NODEV | VFS_FS_VIRTUAL);
    
    // Mount devfs
    vfs_mount_fs("devfs", "/dev", NULL, 0, NULL);

    // Register devices
    devfs_register_device("null", dev_null_read, dev_null_write, NULL);
    devfs_register_device("zero", dev_zero_read, dev_zero_write, NULL);
    devfs_register_device("full", dev_full_read, dev_full_write, NULL);
    devfs_register_device("urandom", dev_urandom_read, dev_urandom_write, NULL);
    devfs_register_device("tty", dev_tty_read, dev_tty_write, NULL);
    devfs_register_device("stdin", NULL, NULL, NULL);
    devfs_register_device("stdout", NULL, NULL, NULL);
    devfs_register_device("stderr", NULL, NULL, NULL);

    vfs_link_std_fd(0, "/dev/stdin");
    vfs_link_std_fd(1, "/dev/stdout");
    vfs_link_std_fd(2, "/dev/stderr");
}