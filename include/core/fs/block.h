#ifndef BLOCK_H
#define BLOCK_H

#include <stddef.h>
#include <stdint.h>
#include <core/fs/vfs.h>

#define MAX_BLOCK_DEVICES 16

// Forward declaration
struct block_device;

// Block device operations
typedef struct {
    // Read `count` blocks starting from `lba` into `buf`.
    int (*read_blocks)(struct block_device* dev, uint64_t lba, size_t count, void* buf);
    // Write `count` blocks starting from `lba` from `buf`.
    int (*write_blocks)(struct block_device* dev, uint64_t lba, size_t count, const void* buf);
} block_device_ops_t;

// Represents a registered block device
typedef struct block_device {
    char name[32];
    uint32_t block_size;    // Size of a single block in bytes
    uint64_t total_blocks;  // Total number of blocks in the device
    block_device_ops_t ops; // Pointers to driver functions
    void* private_data;     // For driver's internal use
    bool used;
} block_device_t;


void block_init(void);

// Functions for drivers to register/unregister themselves
int register_block_device(const char* name, uint32_t block_size, uint64_t total_blocks, 
                           block_device_ops_t* ops, void* private_data);

// Functions for filesystems or VFS to interact with block devices
block_device_t* find_block_device(const char* name);

// For devfs to list devices
block_device_t* get_block_devices(void);


#endif // BLOCK_H
