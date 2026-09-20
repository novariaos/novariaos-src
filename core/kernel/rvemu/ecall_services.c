#include <core/kernel/rvemu/ecall_services.h>

// just for test
static void ecall_service_tty_write(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    uint64_t buffer_address = cpu_read_register(cpu, 10);
    uint64_t buffer_length = cpu_read_register(cpu, 11);

    if (!memory_contains(memory, buffer_address, buffer_length)) {
        host->report_error("ecall tty write: buffer out of bounds", buffer_address, buffer_length, host->context);
        cpu_write_register(cpu, 10, (uint64_t)-1);
        return;
    }

    host->write_output(memory->ram + buffer_address, buffer_length, host->context);
    cpu_write_register(cpu, 10, buffer_length);
}

static void ecall_service_exit(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    (void)memory;
    (void)host;
    cpu->exit_code = (int64_t)cpu_read_register(cpu, 10);
    cpu->halted = 1;
}

void ecall_services_install(ecall_registry_t *registry) {
    ecall_register(registry, RV64_ECALL_EXIT, ecall_service_exit);
    ecall_register(registry, RV64_ECALL_TTY_WRITE, ecall_service_tty_write);
}