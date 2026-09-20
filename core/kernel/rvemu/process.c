#include <core/kernel/rvemu/process.h>

// Allocates the address space and prepares the ecall dispatcher.
int rv64_process_create(rv64_process_t *process, rv64_host_t *host, uint64_t memory_size) {
    process->pid = 0;
    process->host = host;
    process->exit_code = 0;
    process->exited = 0;
    process->active = 0;

    rv64_memory_t *memory = &process->memory;
    if (memory_init(memory, memory_size) != 0) {
        return 1;
    }

    ecall_registry_t *registry = &process->ecall_registry;
    ecall_registry_init(registry);
    return 0;
}

// Resets the CPU and marks the process as runnable.
void rv64_process_start(rv64_process_t *process, uint64_t entry_point, uint64_t stack_pointer) {
    rv64_cpu_t *cpu = &process->cpu;
    ecall_registry_t *registry = &process->ecall_registry;
    cpu_reset(cpu, entry_point, stack_pointer);
    cpu_set_ecall_dispatcher(cpu, ecall_dispatch, registry);
    process->exit_code = 0;
    process->exited = 0;
    process->active = 1;
}

void rv64_process_destroy(rv64_process_t *process) {
    rv64_memory_t *memory = &process->memory;
    memory_destroy(memory);
    process->exit_code = 0;
    process->exited = 0;
    process->active = 0;
}

int rv64_process_step(rv64_process_t *process) {
    rv64_cpu_t *cpu = &process->cpu;
    rv64_memory_t *memory = &process->memory;
    rv64_host_t *host = process->host;
    return cpu_step(cpu, memory, host);
}

void rv64_process_run(rv64_process_t *process) {
    rv64_cpu_t *cpu = &process->cpu;
    rv64_memory_t *memory = &process->memory;
    rv64_host_t *host = process->host;
    cpu_run(cpu, memory, host);
}