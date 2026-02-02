#include <core/kernel/nvm/instructions.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _NVM_H
#define _NVM_H

#define MAX_PROCESSES 32768
#define TIME_SLICE_MS 2
#define MAX_CAPS 16
#define STACK_SIZE 1024
#define MAX_LOCALS 512

typedef struct {
    uint8_t* bytecode;          // Bytecode pointer
    int32_t ip;                 // Instruction Pointer
    int32_t stack[STACK_SIZE];  // Data stack
    int32_t sp;                 // Stack Pointer (changed to 32-bit)
    bool active;                // Process is active?
    uint32_t size;              // Bytecode size
    int32_t exit_code;          // Exit code

    int32_t locals[MAX_LOCALS]; // Legacy globals/local variables (process-scoped)

    // Stack frames
    int32_t fp;                 // Frame pointer (index into stack, -1 if no frame)

    // CAPS
    uint16_t capabilities[MAX_CAPS];  // List of caps
    uint8_t caps_count;               // Count active caps
    uint8_t pid;                      // Process ID

    // Message system
    bool blocked;           // Process blocked waiting for message
    int8_t wakeup_reason;   // Reason for wakeup
} nvm_process_t;

extern nvm_process_t processes[MAX_PROCESSES];
extern uint8_t current_process;
extern uint32_t timer_ticks;

void nvm_init();
void nvm_execute(uint8_t* bytecode, uint32_t size, uint16_t* capabilities, uint8_t caps_count);
int nvm_create_process(uint8_t* bytecode, uint32_t size, uint16_t initial_caps[], uint8_t caps_count);
int nvm_create_process_with_stack(uint8_t* bytecode, uint32_t size,  uint16_t initial_caps[], uint8_t caps_count,  int32_t* initial_stack_values, uint16_t stack_count);
void nvm_scheduler_tick();
bool nvm_is_process_active(uint8_t pid);
int32_t nvm_get_exit_code(uint8_t pid);
nvm_process_t* nvm_get_process(uint8_t pid);

typedef bool (*instruction_handler_t)(nvm_process_t* proc);

#endif
