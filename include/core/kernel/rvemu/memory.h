#ifndef RVEMU_MEMORY_H
#define RVEMU_MEMORY_H

#include <stdint.h>

typedef struct rv64_memory {
    uint8_t *ram;
    uint64_t size;
} rv64_memory_t;

int memory_init(rv64_memory_t *memory, uint64_t size);
void memory_destroy(rv64_memory_t *memory);
int memory_contains(rv64_memory_t *memory, uint64_t address, uint64_t length);

int memory_read_u8(rv64_memory_t *memory, uint64_t address, uint8_t *out_value);
int memory_read_u16(rv64_memory_t *memory, uint64_t address, uint16_t *out_value);
int memory_read_u32(rv64_memory_t *memory, uint64_t address, uint32_t *out_value);
int memory_read_u64(rv64_memory_t *memory, uint64_t address, uint64_t *out_value);

int memory_write_u8(rv64_memory_t *memory, uint64_t address, uint8_t value);
int memory_write_u16(rv64_memory_t *memory, uint64_t address, uint16_t value);
int memory_write_u32(rv64_memory_t *memory, uint64_t address, uint32_t value);
int memory_write_u64(rv64_memory_t *memory, uint64_t address, uint64_t value);

int memory_copy_in(rv64_memory_t *memory, uint64_t address, const uint8_t *data, uint64_t length);
int memory_zero_range(rv64_memory_t *memory, uint64_t address, uint64_t length);

#endif