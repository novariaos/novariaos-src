// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/block.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>
#include <string.h>

static block_device_t block_devices[MAX_BLOCK_DEVICES];

void block_init(void) {
    memset(block_devices, 0, sizeof(block_devices));
    LOG_INFO("Block device layer initialized.");
}

int register_block_device(const char* name, uint32_t block_size, uint64_t total_blocks, 
                           block_device_ops_t* ops, void* private_data) {
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (!block_devices[i].used) {
            strcpy_safe(block_devices[i].name, name, sizeof(block_devices[i].name));
            block_devices[i].block_size = block_size;
            block_devices[i].total_blocks = total_blocks;
            block_devices[i].ops = *ops;
            block_devices[i].private_data = private_data;
            block_devices[i].used = true;
            LOG_INFO("Registered block device '%s'.", name);
            return 0;
        }
    }

    LOG_WARN("Could not register block device '%s': registry full.", name);
    return -ENOMEM;
}

block_device_t* find_block_device(const char* name) {
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (block_devices[i].used && strcmp(block_devices[i].name, name) == 0) {
            return &block_devices[i];
        }
    }
    return NULL;
}

block_device_t* get_block_devices(void) {
    return block_devices;
}
