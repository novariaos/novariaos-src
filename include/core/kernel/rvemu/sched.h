#ifndef RVEMU_SCHEDULER_H
#define RVEMU_SCHEDULER_H

#include <stdint.h>

#include <core/kernel/rvemu/process.h>

#define RV64_SCHEDULER_MAX_PROCESSES 32

typedef struct rv64_scheduler {
    rv64_process_t *processes[RV64_SCHEDULER_MAX_PROCESSES];
    int process_count;
    int current_index;
    uint64_t instructions_per_slice;
} rv64_scheduler_t;

void rv64_scheduler_init(rv64_scheduler_t *scheduler, uint64_t instructions_per_slice);
int rv64_scheduler_add(rv64_scheduler_t *scheduler, rv64_process_t *process);
int rv64_scheduler_remove(rv64_scheduler_t *scheduler, rv64_process_t *process);
int rv64_scheduler_has_running(rv64_scheduler_t *scheduler);
void rv64_scheduler_tick(rv64_scheduler_t *scheduler);
void rv64_scheduler_run(rv64_scheduler_t *scheduler);

#endif