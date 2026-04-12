// SPDX-License-Identifier: GPL-3.0-only

#ifndef NVM_H
#define NVM_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PROCESSES 32
#define STACK_SIZE 1024
#define MAX_LOCALS 256
#define MAX_CAPS 8
#define TIME_SLICE_MS 10

typedef struct {
    uint8_t* bytecode;
    uint32_t ip;
    uint32_t size;
    int32_t stack[STACK_SIZE];
    uint16_t sp;
    int32_t locals[MAX_LOCALS];
    bool active;
    bool blocked;
    int32_t exit_code;
    uint8_t pid;
    int32_t fp;
    uint8_t wakeup_reason;
    
    // Capabilities
    uint16_t capabilities[MAX_CAPS];
    uint8_t caps_count;
    
    // Heap
    uint8_t* heap;
    uint32_t heap_size;
} nvm_process_t;

extern nvm_process_t processes[MAX_PROCESSES];
extern uint8_t current_process;

int nvm_create_process(uint8_t* bytecode, uint32_t size, uint16_t initial_caps[], uint8_t caps_count);
int nvm_create_process_with_stack(uint8_t* bytecode, uint32_t size, uint16_t initial_caps[], uint8_t caps_count, int32_t* initial_stack_values, uint16_t stack_count);
bool nvm_execute_instruction(nvm_process_t* proc);
void nvm_scheduler_tick();
nvm_process_t* nvm_get_process(uint8_t pid);
void nvm_execute(uint8_t* bytecode, uint32_t size, uint16_t* capabilities, uint8_t caps_count);
int32_t nvm_get_exit_code(uint8_t pid);
bool nvm_is_process_active(uint8_t pid);
void nvm_init(void);

#endif