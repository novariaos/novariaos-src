#ifndef DEVFS_H
#define DEVFS_H

#include <core/fs/vfs.h>

void devfs_init(void);
void devfs_register_device(const char* name, vfs_dev_read_t read_fn,
                           vfs_dev_write_t write_fn, void* data);

#endif