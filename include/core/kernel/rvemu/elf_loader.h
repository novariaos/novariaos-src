#ifndef RVEMU_ELF_LOADER_H
#define RVEMU_ELF_LOADER_H

#include <stdint.h>
#include <core/kernel/rvemu/memory.h>

int elf_loader_load(const uint8_t *image, uint64_t image_size, rv64_memory_t *memory, uint64_t *entry_point);

#endif