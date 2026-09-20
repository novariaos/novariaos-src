#ifndef RVEMU_RVEMU_H
#define RVEMU_RVEMU_H

#include <stdbool.h>
#include <stdint.h>

#include <core/kernel/rvemu/process.h>

#define RV64_MAX_PROCESSES 32

extern int current_process;

int rvemu_init(void);
int rvemu_create_process_from_elf(const uint8_t *image, uint32_t size);
void rvemu_tick(void);
rv64_process_t *rvemu_get_process(uint64_t pid);
bool rvemu_is_process_active(uint64_t pid);
int32_t rvemu_get_exit_code(uint64_t pid);
int rvemu_reap_process(uint64_t pid);

#endif