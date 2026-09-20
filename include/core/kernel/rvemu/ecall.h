#ifndef RVEMU_ECALL_H
#define RVEMU_ECALL_H

#include <stdint.h>

#include <core/kernel/rvemu/cpu.h>
#include <core/kernel/rvemu/host.h>
#include <core/kernel/rvemu/memory.h>

#define ECALL_TABLE_CAPACITY 32

typedef void (*ecall_handler_t)(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host);

typedef struct ecall_entry {
    uint32_t number;
    ecall_handler_t handler;
} ecall_entry_t;

typedef struct ecall_registry {
    ecall_entry_t entries[ECALL_TABLE_CAPACITY];
    int count;
} ecall_registry_t;

void ecall_registry_init(ecall_registry_t *registry);
int ecall_register(ecall_registry_t *registry, uint32_t number, ecall_handler_t handler);
void ecall_dispatch(void *registry_context, rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host);

#endif