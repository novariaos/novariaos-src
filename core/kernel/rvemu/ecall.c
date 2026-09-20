#include <core/kernel/rvemu/ecall.h>

void ecall_registry_init(ecall_registry_t *registry) {
    registry->count = 0;
    for (int index = 0; index < ECALL_TABLE_CAPACITY; index++) {
        registry->entries[index].number = 0;
        registry->entries[index].handler = NULL;
    }
}

int ecall_register(ecall_registry_t *registry, uint32_t number, ecall_handler_t handler) {
    for (int index = 0; index < registry->count; index++) {
        if (registry->entries[index].number == number) {
            return 1;
        }
    }
    if (registry->count >= ECALL_TABLE_CAPACITY) {
        return 1;
    }
    registry->entries[registry->count].number = number;
    registry->entries[registry->count].handler = handler;
    registry->count = registry->count + 1;
    return 0;
}

void ecall_dispatch(void *registry_context, rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    ecall_registry_t *registry = (ecall_registry_t *)registry_context;
    uint32_t number = (uint32_t)cpu_read_register(cpu, 17);

    for (int index = 0; index < registry->count; index++) {
        if (registry->entries[index].number == number) {
            ecall_handler_t handler = registry->entries[index].handler;
            handler(cpu, memory, host);
            return;
        }
    }

    host->report_error("ecall: unknown syscall number", number, 0, host->context);
    cpu->halted = 1;
}