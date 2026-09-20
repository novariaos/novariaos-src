#include <core/fs/procfs.h>
#include <core/kernel/rvemu/ecall_services.h>
#include <core/kernel/rvemu/elf_loader.h>
#include <core/kernel/rvemu/rvemu.h>
#include <core/kernel/rvemu/sched.h>

#define RV64_MEMORY_SIZE (256 * 1024)
#define RV64_STACK_POINTER_ALIGNMENT 16
#define RV64_SCHEDULER_SLICE_INSTRUCTIONS 5000

static rv64_process_t processes[RV64_MAX_PROCESSES];
static rv64_scheduler_t rvemu_scheduler;
static rv64_host_t rvemu_host;

int current_process = 0;

static rv64_process_t *find_free_process_slot(void) {
    for (int index = 0; index < RV64_MAX_PROCESSES; index++) {
        if (processes[index].active == 0) {
            return &processes[index];
        }
    }
    return NULL;
}

int rvemu_init(void) {
    for (int index = 0; index < RV64_MAX_PROCESSES; index++) {
        processes[index].active = 0;
        processes[index].exited = 0;
    }
    rvemu_host_init(&rvemu_host);
    rv64_scheduler_init(&rvemu_scheduler, RV64_SCHEDULER_SLICE_INSTRUCTIONS);
    return 0;
}

int rvemu_create_process_from_elf(const uint8_t *image, uint32_t size) {
    rv64_process_t *process = find_free_process_slot();
    if (process == NULL) {
        return -1;
    }

    if (rv64_process_create(process, &rvemu_host, RV64_MEMORY_SIZE) != 0) {
        return -1;
    }
    ecall_services_install(&process->ecall_registry);

    uint64_t entry_point = 0;
    if (elf_loader_load(image, size, &process->memory, &entry_point) != 0) {
        rv64_process_destroy(process);
        return -1;
    }

    if (rv64_scheduler_add(&rvemu_scheduler, process) != 0) {
        rv64_process_destroy(process);
        return -1;
    }

    uint64_t stack_pointer = process->memory.size & ~((uint64_t)RV64_STACK_POINTER_ALIGNMENT - 1);
    rv64_process_start(process, entry_point, stack_pointer);
    procfs_register((int)process->pid, process);
    return (int)process->pid;
}

void rvemu_tick(void) {
    for (int index = 0; index < RV64_MAX_PROCESSES; index++) {
        rv64_process_t *process = &processes[index];
        if (process->active != 0 && process->cpu.halted != 0) {
            rvemu_reap_process(process->pid);
        }
    }
    rv64_scheduler_tick(&rvemu_scheduler);
}

rv64_process_t *rvemu_get_process(uint64_t pid) {
    for (int index = 0; index < RV64_MAX_PROCESSES; index++) {
        if (processes[index].active != 0 && processes[index].pid == pid) {
            return &processes[index];
        }
    }
    return NULL;
}

bool rvemu_is_process_active(uint64_t pid) {
    rv64_process_t *process = rvemu_get_process(pid);
    if (process == NULL) {
        return false;
    }
    return process->cpu.halted == 0;
}

int32_t rvemu_get_exit_code(uint64_t pid) {
    rv64_process_t *process = rvemu_get_process(pid);
    if (process == NULL) {
        return -1;
    }
    return process->cpu.exit_code;
}

int rvemu_reap_process(uint64_t pid) {
    rv64_process_t *process = rvemu_get_process(pid);
    if (process == NULL) {
        return 1;
    }
    rv64_scheduler_remove(&rvemu_scheduler, process);
    rv64_process_destroy(process);
    procfs_unregister((int)pid);
    return 0;
}