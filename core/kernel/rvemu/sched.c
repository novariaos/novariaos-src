#include <core/kernel/rvemu/sched.h>

void rv64_scheduler_init(rv64_scheduler_t *scheduler, uint64_t instructions_per_slice) {
    for (int index = 0; index < RV64_SCHEDULER_MAX_PROCESSES; index++) {
        scheduler->processes[index] = NULL;
    }
    scheduler->process_count = 0;
    scheduler->current_index = 0;
    scheduler->instructions_per_slice = instructions_per_slice;
}

int rv64_scheduler_add(rv64_scheduler_t *scheduler, rv64_process_t *process) {
    for (int index = 0; index < scheduler->process_count; index++) {
        if (scheduler->processes[index] == process) {
            return 1;
        }
    }
    if (scheduler->process_count >= RV64_SCHEDULER_MAX_PROCESSES) {
        return 1;
    }
    process->pid = (uint64_t)scheduler->process_count;
    scheduler->processes[scheduler->process_count] = process;
    scheduler->process_count = scheduler->process_count + 1;
    return 0;
}

int rv64_scheduler_remove(rv64_scheduler_t *scheduler, rv64_process_t *process) {
    for (int index = 0; index < scheduler->process_count; index++) {
        if (scheduler->processes[index] == process) {
            for (int shift = index; shift < scheduler->process_count - 1; shift++) {
                scheduler->processes[shift] = scheduler->processes[shift + 1];
            }
            scheduler->process_count = scheduler->process_count - 1;
            scheduler->processes[scheduler->process_count] = NULL;
            if (scheduler->current_index >= scheduler->process_count) {
                scheduler->current_index = 0;
            }
            return 0;
        }
    }
    return 1;
}

int rv64_scheduler_has_running(rv64_scheduler_t *scheduler) {
    for (int index = 0; index < scheduler->process_count; index++) {
        rv64_process_t *process = scheduler->processes[index];
        rv64_cpu_t *cpu = &process->cpu;
        if (cpu->halted == 0) {
            return 1;
        }
    }
    return 0;
}

void rv64_scheduler_tick(rv64_scheduler_t *scheduler) {
    if (scheduler->process_count == 0) {
        return;
    }

    int original = scheduler->current_index;
    int picked = original;
    int found = 0;
    for (int offset = 1; offset <= scheduler->process_count; offset++) {
        int index = (original + offset) % scheduler->process_count;
        rv64_process_t *candidate = scheduler->processes[index];
        rv64_cpu_t *candidate_cpu = &candidate->cpu;
        if (candidate_cpu->halted == 0) {
            picked = index;
            found = 1;
            break;
        }
    }

    if (!found) {
        scheduler->current_index = original;
        return;
    }

    scheduler->current_index = picked;
    rv64_process_t *process = scheduler->processes[picked];
    for (uint64_t executed = 0; executed < scheduler->instructions_per_slice; executed++) {
        rv64_cpu_t *cpu = &process->cpu;
        if (cpu->halted) {
            return;
        }
        if (rv64_process_step(process) != 0) {
            return;
        }
    }
}

void rv64_scheduler_run(rv64_scheduler_t *scheduler) {
    while (rv64_scheduler_has_running(scheduler)) {
        rv64_scheduler_tick(scheduler);
    }
}