#ifndef RVEMU_PROCESS_H
#define RVEMU_PROCESS_H

#include <stdint.h>

#include <core/kernel/rvemu/cpu.h>
#include <core/kernel/rvemu/ecall.h>
#include <core/kernel/rvemu/host.h>
#include <core/kernel/rvemu/memory.h>

typedef struct rv64_process {
    uint64_t pid;
    rv64_cpu_t cpu;
    rv64_memory_t memory;
    ecall_registry_t ecall_registry;
    rv64_host_t *host;
    int exit_code;
    int exited;
    int active;
} rv64_process_t;

int rv64_process_create(rv64_process_t *process, rv64_host_t *host, uint64_t memory_size);
void rv64_process_start(rv64_process_t *process, uint64_t entry_point, uint64_t stack_pointer);
void rv64_process_destroy(rv64_process_t *process);
int rv64_process_step(rv64_process_t *process);
void rv64_process_run(rv64_process_t *process);

#endif