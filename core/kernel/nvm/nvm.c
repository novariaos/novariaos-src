// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/syscall.h>
#include <core/drivers/serial.h>
#include <core/kernel/nvm/nvm.h>
#include <core/kernel/nvm/caps.h>
#include <core/kernel/kstd.h>
#include <log.h>
#include <core/fs/procfs.h>
#include <stdint.h>

nvm_process_t processes[MAX_PROCESSES];
uint8_t current_process = 0;
uint32_t timer_ticks = 0;

static instruction_handler_t instruction_table[256] = {NULL};

// Signature checking and process creation
int nvm_create_process(uint8_t* bytecode, uint32_t size, uint16_t initial_caps[], uint8_t caps_count) {
    if(bytecode[0] != 0x4E || bytecode[1] != 0x56 ||
       bytecode[2] != 0x4D || bytecode[3] != 0x30) {
        LOG_WARN("Invalid NVM signature\n");
        return -1;
    }

    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(!processes[i].active) {
            processes[i].bytecode = bytecode;
            processes[i].ip = 4;
            processes[i].size = size;
            processes[i].sp = 0;
            processes[i].active = true;
            processes[i].exit_code = 0;
            processes[i].pid = i;
            processes[i].caps_count = 0;
            processes[i].fp = -1;

            // Initializing capabilities
            for(int j = 0; j < caps_count && j < MAX_CAPS; j++) {
                processes[i].capabilities[j] = initial_caps[j];
            }
            processes[i].caps_count = caps_count;

            for(int j = 0; j < MAX_LOCALS; j++) {
                processes[i].locals[j] = 0;
            }

            procfs_register(i, &processes[i]);
            return i;
        }
    }

    LOG_WARN("No free process slots\n");
    return -1;
}

int nvm_create_process_with_stack(uint8_t* bytecode, uint32_t size,
                                  uint16_t initial_caps[], uint8_t caps_count,
                                  int32_t* initial_stack_values, uint16_t stack_count) {
    if(bytecode[0] != 0x4E || bytecode[1] != 0x56 ||
       bytecode[2] != 0x4D || bytecode[3] != 0x30) {
        LOG_WARN("Invalid NVM signature\n");
        return -1;
    }

    // Validate stack count
    if(stack_count > STACK_SIZE) {
        LOG_WARN("Initial stack count %d exceeds STACK_SIZE %d\n", stack_count, STACK_SIZE);
        return -1;
    }

    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(!processes[i].active) {
            processes[i].bytecode = bytecode;
            processes[i].ip = 4;
            processes[i].size = size;
            processes[i].active = true;
            processes[i].exit_code = 0;
            processes[i].pid = i;
            processes[i].caps_count = 0;
            processes[i].blocked = false;
            processes[i].wakeup_reason = 0;
            processes[i].fp = -1;

            for(int j = 0; j < stack_count; j++) {
                processes[i].stack[j] = initial_stack_values[j];
            }
            
            if(stack_count > 0) {
                int argc = initial_stack_values[0];
                
                if(argc > 0) {
                    int argv_pointers_start = 1;
                    int strings_start = 1 + argc;

                    for(int arg_idx = 0; arg_idx < argc; arg_idx++) {
                        int string_pointer = initial_stack_values[argv_pointers_start + arg_idx];
                        if(string_pointer >= 0 && string_pointer < stack_count) {
                            int str_start = string_pointer;
                            int str_end = str_start;
                            
                            while(str_end < stack_count && initial_stack_values[str_end] != 0) {
                                str_end++;
                            }

                            int len = str_end - str_start;
                            for(int k = 0; k < len / 2; k++) {
                                int32_t temp = processes[i].stack[str_start + k];
                                processes[i].stack[str_start + k] = processes[i].stack[str_end - 1 - k];
                                processes[i].stack[str_end - 1 - k] = temp;
                            }
                        }
                    }
                }
            }
            
            processes[i].sp = stack_count;

            // Initializing capabilities
            for(int j = 0; j < caps_count && j < MAX_CAPS; j++) {
                processes[i].capabilities[j] = initial_caps[j];
            }
            processes[i].caps_count = caps_count;

            // Initialize locals
            for(int j = 0; j < MAX_LOCALS; j++) {
                processes[i].locals[j] = 0;
            }

            procfs_register(i, &processes[i]);
            return i;
        }
    }

    LOG_WARN("No free process slots\n");
    return -1;
}

// Execute one instruction
bool nvm_execute_instruction(nvm_process_t* proc) {
    if(proc->ip >= proc->size) {
        LOG_WARN("process %d: Instruction pointer out of bounds\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    
    uint8_t opcode = proc->bytecode[proc->ip++];
    
    if(instruction_table[opcode] != NULL) {
        return instruction_table[opcode](proc);
    }
    
    return true;
}

// Round Robin task manager
void nvm_scheduler_tick() {
    timer_ticks++;
    if(timer_ticks % TIME_SLICE_MS != 0) {
        return;
    }
    
    uint8_t start = current_process;
    uint8_t original = current_process;

    do {
        current_process = (current_process + 1) % MAX_PROCESSES;
        if(processes[current_process].active && !processes[current_process].blocked) {
            break;
        }
    } while(current_process != start);
    
    if(processes[current_process].active && !processes[current_process].blocked) {
        for(int i = 0; i < 5000; i++) {
            if (processes[current_process].ip < processes[current_process].size && 
                processes[current_process].active && 
                !processes[current_process].blocked) {
                if(!nvm_execute_instruction(&processes[current_process])) {
                    break; // Stop if instruction returns false (halt, error, etc)
                }
            } else {
                if(processes[current_process].ip >= processes[current_process].size && 
                   processes[current_process].active) {
                    processes[current_process].active = false;
                    processes[current_process].exit_code = 0;
                }
                break;
            }
        }
    } else {
        current_process = original;
    }
}

nvm_process_t* nvm_get_process(uint8_t pid) {
    return &processes[pid];
}

void nvm_execute(uint8_t* bytecode, uint32_t size, uint16_t* capabilities, uint8_t caps_count) {
    int pid = nvm_create_process(bytecode, size, capabilities, caps_count);
    if(pid >= 0) {
        if (caps_count > 0) {
            LOG_INFO("NVM process started with PID: %d\n", pid);
        }
    }
}

// Function for get exit code
int32_t nvm_get_exit_code(uint8_t pid) {
    if(pid < MAX_PROCESSES && !processes[pid].active) {
        return processes[pid].exit_code;
    }
    return -1;
}

// Function for check process activity
bool nvm_is_process_active(uint8_t pid) {
    if(pid < MAX_PROCESSES) {
        return processes[pid].active;
    }
    return false;
}

void nvm_init_instruction_table(void) {
    for(int i = 0; i < 256; i++) {
        instruction_table[i] = NULL;
    }
    
    instruction_table[0x00] = handle_halt;
    instruction_table[0x01] = handle_nop;
    instruction_table[0x02] = handle_push;
    instruction_table[0x04] = handle_pop;
    instruction_table[0x05] = handle_dup;
    instruction_table[0x06] = handle_swap;
    
    instruction_table[0x10] = handle_add;
    instruction_table[0x11] = handle_sub;
    instruction_table[0x12] = handle_mul;
    instruction_table[0x13] = handle_div;
    instruction_table[0x14] = handle_mod;
    
    instruction_table[0x20] = handle_cmp;
    instruction_table[0x21] = handle_eq;
    instruction_table[0x22] = handle_neq;
    instruction_table[0x23] = handle_gt;
    instruction_table[0x24] = handle_lt;
    
    instruction_table[0x30] = handle_jmp;
    instruction_table[0x31] = handle_jz;
    instruction_table[0x32] = handle_jnz;
    instruction_table[0x33] = handle_call;
    instruction_table[0x34] = handle_ret;
    
    instruction_table[0x35] = handle_enter;
    instruction_table[0x36] = handle_leave;
    instruction_table[0x37] = handle_load_arg;
    instruction_table[0x38] = handle_store_arg;
    
    instruction_table[0x40] = handle_load;
    instruction_table[0x41] = handle_store;
    instruction_table[0x42] = handle_load_rel;
    instruction_table[0x43] = handle_store_rel;
    instruction_table[0x44] = handle_load_abs;
    instruction_table[0x45] = handle_store_abs;
    
    instruction_table[0x50] = handle_syscall;
    instruction_table[0x51] = handle_break;
}


void nvm_init() {
    for(int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].active = false;
        processes[i].sp = 0;
        processes[i].ip = 0;
        processes[i].exit_code = 0;
        processes[i].caps_count = 0;
        processes[i].fp = -1;
    }

    nvm_init_instruction_table();
    kprint(":: NVM initialized\n", 7);
}