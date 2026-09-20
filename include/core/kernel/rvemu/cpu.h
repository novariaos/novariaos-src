#ifndef RVEMU_CPU_H
#define RVEMU_CPU_H

#include <stddef.h>
#include <stdint.h>
#include <core/kernel/rvemu/host.h>
#include <core/kernel/rvemu/memory.h>

#define CPU_GENERAL_REGISTER_COUNT 32

typedef struct rv64_cpu rv64_cpu_t;
typedef void (*ecall_dispatcher_fn)(void *dispatcher_context, rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host);

struct rv64_cpu {
    uint64_t registers[CPU_GENERAL_REGISTER_COUNT];
    uint64_t pc;
    int halted;
    int trace_enabled;
    ecall_dispatcher_fn ecall_dispatcher;
    void *ecall_dispatcher_context;
};

void cpu_reset(rv64_cpu_t *cpu, uint64_t entry_point, uint64_t stack_pointer);
void cpu_set_ecall_dispatcher(rv64_cpu_t *cpu, ecall_dispatcher_fn dispatcher, void *dispatcher_context);
uint64_t cpu_read_register(rv64_cpu_t *cpu, int index);
void cpu_write_register(rv64_cpu_t *cpu, int index, uint64_t value);
int cpu_step(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host);
void cpu_run(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host);

#endif