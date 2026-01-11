// SPDX-License-Identifier: GPL-3.0-only

#include <core/crypto/chacha20_rng.h>
#include <core/arch/entropy.h>
#include <core/kernel/kstd.h>
#include <core/fs/vfs.h>
#include <string.h>

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
    
    chacha20_rng_bytes(&rng, buf, count);
    
    return count;
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

static vfs_ssize_t dev_console_read(vfs_file_t* file, void* buf, size_t count, vfs_off_t* pos) {
    (void)file; (void)pos;
    
    return 1;
}

static vfs_ssize_t dev_console_write(vfs_file_t* file, const void* buf, size_t count, vfs_off_t* pos) {
    kprint(buf, 7);
    return 0;
}

void devfs_init() {
    vfs_pseudo_register_with_fd("/dev/null", DEV_NULL_FD, dev_null_read, dev_null_write, dev_null_seek, NULL, NULL);
    vfs_pseudo_register_with_fd("/dev/zero", DEV_ZERO_FD, dev_zero_read, dev_zero_write, NULL, NULL, NULL);
    vfs_pseudo_register_with_fd("/dev/full", DEV_FULL_FD, dev_full_read, dev_full_write, NULL, NULL, NULL);
    
    vfs_pseudo_register("/dev/urandom", dev_urandom_read, dev_urandom_write, NULL, NULL, NULL);
    vfs_pseudo_register("/dev/console", dev_console_read, dev_console_write, NULL, NULL, NULL);

    vfs_pseudo_register_with_fd("/dev/stdin", DEV_STDIN_FD, NULL, NULL, NULL, NULL, NULL);
    vfs_pseudo_register_with_fd("/dev/stdout", DEV_STDOUT_FD, NULL, NULL, NULL, NULL, NULL);
    vfs_pseudo_register_with_fd("/dev/stderr", DEV_STDERR_FD, NULL, NULL, NULL, NULL, NULL);
    
    vfs_link_std_fd(0, "/dev/stdin");
    vfs_link_std_fd(1, "/dev/stdout");
    vfs_link_std_fd(2, "/dev/stderr");
}