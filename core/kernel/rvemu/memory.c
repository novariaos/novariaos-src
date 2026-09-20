#include <core/kernel/mem/allocator.h>
#include <core/kernel/rvemu/memory.h>

int memory_init(rv64_memory_t *memory, uint64_t size) {
    uint64_t attempt = size;
    while (attempt >= (64 * 1024)) {
        uint8_t *ram = (uint8_t *)kmalloc((size_t)attempt);
        if (ram != NULL) {
            for (uint64_t index = 0; index < attempt; index++) {
                ram[index] = 0;
            }
            memory->ram = ram;
            memory->size = attempt;
            return 0;
        }
        attempt /= 2;
    }

    memory->ram = NULL;
    memory->size = 0;
    return 1;
}

void memory_destroy(rv64_memory_t *memory) {
    kfree(memory->ram);
    memory->ram = NULL;
    memory->size = 0;
}

int memory_contains(rv64_memory_t *memory, uint64_t address, uint64_t length) {
    if (address > memory->size) {
        return 0;
    }
    if (length > memory->size - address) {
        return 0;
    }
    return 1;
}

int memory_read_u8(rv64_memory_t *memory, uint64_t address, uint8_t *out_value) {
    if (!memory_contains(memory, address, 1)) {
        return 1;
    }
    *out_value = memory->ram[address];
    return 0;
}

int memory_read_u16(rv64_memory_t *memory, uint64_t address, uint16_t *out_value) {
    if (!memory_contains(memory, address, 2)) {
        return 1;
    }
    *out_value = (uint16_t)memory->ram[address]
               | ((uint16_t)memory->ram[address + 1] << 8);
    return 0;
}

int memory_read_u32(rv64_memory_t *memory, uint64_t address, uint32_t *out_value) {
    if (!memory_contains(memory, address, 4)) {
        return 1;
    }
    *out_value = (uint32_t)memory->ram[address]
               | ((uint32_t)memory->ram[address + 1] << 8)
               | ((uint32_t)memory->ram[address + 2] << 16)
               | ((uint32_t)memory->ram[address + 3] << 24);
    return 0;
}

int memory_read_u64(rv64_memory_t *memory, uint64_t address, uint64_t *out_value) {
    if (!memory_contains(memory, address, 8)) {
        return 1;
    }
    *out_value = (uint64_t)memory->ram[address]
               | ((uint64_t)memory->ram[address + 1] << 8)
               | ((uint64_t)memory->ram[address + 2] << 16)
               | ((uint64_t)memory->ram[address + 3] << 24)
               | ((uint64_t)memory->ram[address + 4] << 32)
               | ((uint64_t)memory->ram[address + 5] << 40)
               | ((uint64_t)memory->ram[address + 6] << 48)
               | ((uint64_t)memory->ram[address + 7] << 56);
    return 0;
}

int memory_write_u8(rv64_memory_t *memory, uint64_t address, uint8_t value) {
    if (!memory_contains(memory, address, 1)) {
        return 1;
    }
    memory->ram[address] = value;
    return 0;
}

int memory_write_u16(rv64_memory_t *memory, uint64_t address, uint16_t value) {
    if (!memory_contains(memory, address, 2)) {
        return 1;
    }
    memory->ram[address] = (uint8_t)(value & 0xFF);
    memory->ram[address + 1] = (uint8_t)((value >> 8) & 0xFF);
    return 0;
}

int memory_write_u32(rv64_memory_t *memory, uint64_t address, uint32_t value) {
    if (!memory_contains(memory, address, 4)) {
        return 1;
    }
    memory->ram[address] = (uint8_t)(value & 0xFF);
    memory->ram[address + 1] = (uint8_t)((value >> 8) & 0xFF);
    memory->ram[address + 2] = (uint8_t)((value >> 16) & 0xFF);
    memory->ram[address + 3] = (uint8_t)((value >> 24) & 0xFF);
    return 0;
}

int memory_write_u64(rv64_memory_t *memory, uint64_t address, uint64_t value) {
    if (!memory_contains(memory, address, 8)) {
        return 1;
    }
    memory->ram[address] = (uint8_t)(value & 0xFF);
    memory->ram[address + 1] = (uint8_t)((value >> 8) & 0xFF);
    memory->ram[address + 2] = (uint8_t)((value >> 16) & 0xFF);
    memory->ram[address + 3] = (uint8_t)((value >> 24) & 0xFF);
    memory->ram[address + 4] = (uint8_t)((value >> 32) & 0xFF);
    memory->ram[address + 5] = (uint8_t)((value >> 40) & 0xFF);
    memory->ram[address + 6] = (uint8_t)((value >> 48) & 0xFF);
    memory->ram[address + 7] = (uint8_t)((value >> 56) & 0xFF);
    return 0;
}

int memory_copy_in(rv64_memory_t *memory, uint64_t address, const uint8_t *data, uint64_t length) {
    if (!memory_contains(memory, address, length)) {
        return 1;
    }
    for (uint64_t index = 0; index < length; index++) {
        memory->ram[address + index] = data[index];
    }
    return 0;
}

int memory_zero_range(rv64_memory_t *memory, uint64_t address, uint64_t length) {
    if (!memory_contains(memory, address, length)) {
        return 1;
    }
    for (uint64_t index = 0; index < length; index++) {
        memory->ram[address + index] = 0;
    }
    return 0;
}