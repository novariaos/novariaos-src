#ifndef BLOCK_DEV_VFS_H
#define BLOCK_DEV_VFS_H

#include <core/fs/block.h>

void block_dev_vfs_init(void);
void block_dev_vfs_register_one(block_device_t* dev);

#endif // BLOCK_DEV_VFS_H
