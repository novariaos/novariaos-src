// SPDX-License-Identifier: GPL-3.0-only

#include <core/fs/initramfs.h>
#include <core/kernel/mem.h>
#include <stdint.h>
#include <stddef.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>

#define MAX_PROGRAMS 64

static struct program programs[MAX_PROGRAMS];
static size_t program_count = 0;

void initramfs_load_from_memory(void* initramfs_data, size_t initramfs_size) {
    if (!initramfs_data || initramfs_size == 0) {
        LOG_WARN("initramfs: No data provided.\n");
        return;
    }

    uint8_t *data = (uint8_t*)initramfs_data;
    size_t offset = 0;
    program_count = 0;

    LOG_DEBUG("initramfs: Loading from memory (size=%d)..\n", initramfs_size);

    while (offset < initramfs_size && program_count < MAX_PROGRAMS) {
        if (offset + 4 > initramfs_size) {
            LOG_WARN("initramfs: Error: Incomplete size field.\n");
            break;
        }

        uint32_t prog_size = (data[offset] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | data[offset+3];
        offset += 4;

        if (prog_size == 0 || prog_size > initramfs_size - offset) {
            LOG_WARN("initramfs: Error: Invalid program size %d.\n", prog_size);
            break;
        }

        programs[program_count].data = (const char*)&data[offset];
        programs[program_count].size = prog_size;
        
        program_count++;
        offset += prog_size;

        // Align to next 4-byte boundary
        size_t padding = (4 - (offset % 4)) % 4;
        offset += padding;
    }

    LOG_DEBUG("initramfs: Total programs loaded: %d\n", program_count);
}

struct program* initramfs_get_program(size_t index) {
    if (index >= program_count) return NULL;
    return &programs[index];
}

size_t initramfs_get_count(void) {
    return program_count;
}