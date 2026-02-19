#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <stdint.h>
#include <stddef.h>
#include <lib/limine.h>

struct program {
    const char* data;
    size_t size;
    int ramfs_sector;
};

void initramfs_load(struct limine_module_request* module_request);
void initramfs_load_limine(volatile struct limine_module_request* module_request);
void initramfs_load_from_memory(void* initramfs_data, size_t initramfs_size);
struct program* initramfs_get_program(size_t index);
size_t initramfs_get_count(void);
int initramfs_load_to_ramfs(size_t index);
void initramfs_list_programs(void);

#endif