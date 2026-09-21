#include <core/kernel/rvemu/ecall_services.h>
#include <core/kernel/kstd.h>
#include <core/fs/vfs.h>
#include <core/kernel/rvemu/rvemu.h>
#include <core/kernel/mem.h>

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

static void ecall_service_spawn(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    uint64_t bin_path = cpu_read_register(cpu, 10);
    char *bin_path_str = (char *)(memory->ram + bin_path);
    
    if (vfs_exists(bin_path_str)) {
        size_t size;
        const char* data = vfs_read(bin_path_str, &size);
                
        if (data && size > 0) {                
            uint8_t* bytecode = kmalloc(size);
            if (!bytecode) {
                kprint("Error: Memory allocation failed\n", 12);
                return;
            }
                    
            for (size_t i = 0; i < size; i++) {
                bytecode[i] = (uint8_t)data[i];
            }
                    
            int pid = rvemu_create_process_from_elf(bytecode, size);
            kfree(bytecode);

            if (pid < 0) {
                kprint("Error: Failed to create process\n", 12);
                return;
            }

            return;
            } else {
                kprint("Error: Failed to read program file\n", 12);
            }
    } else {
        kprint("Error: Failed to find bin\n", 7);
    }
}

static void ecall_service_sleep(rv64_cpu_t *cpu, rv64_memory_t *memory, rv64_host_t *host) {
    uint64_t milliseconds = cpu_read_register(cpu, 10);
    
    sleep(milliseconds);
}

void ecall_services_install(ecall_registry_t *registry) {
    ecall_register(registry, RV64_ECALL_EXIT, ecall_service_exit);
    ecall_register(registry, RV64_ECALL_TTY_WRITE, ecall_service_tty_write);
    ecall_register(registry, RV64_ECALL_SPAWN, ecall_service_spawn);
    ecall_register(registry, RV64_ECALL_SLEEP, ecall_service_sleep);
}