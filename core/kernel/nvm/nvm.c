// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/syscall.h>
#include <core/drivers/serial.h>
#include <core/kernel/nvm/nvm.h>
#include <core/kernel/nvm/caps.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>
#include <core/fs/procfs.h>
#include <stdint.h>

nvm_process_t processes[MAX_PROCESSES];
uint8_t current_process = 0;
uint32_t timer_ticks = 0;

int32_t syscall_handler(uint8_t syscall_id, nvm_process_t* proc);

void nvm_init() {
    for(int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].active = false;
        processes[i].sp = 0;
        processes[i].ip = 0;
        processes[i].exit_code = 0;
        processes[i].caps_count = 0;
    }

    kprint(":: NVM initialized\n", 7);
}

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
    
    switch(opcode) {
        // Basic:
        case 0x00: // HALT
            proc->active = false;
            proc->exit_code = 0;
            LOG_DEBUG("process %d: Halted\n", proc->pid);
            return false;
        
        case 0x01: // NOP
            break;
            
        case 0x02: // PUSH
            if(proc->ip + 3 < proc->size) {
                uint32_t value = (proc->bytecode[proc->ip] << 24) |
                                (proc->bytecode[proc->ip + 1] << 16) |
                                (proc->bytecode[proc->ip + 2] << 8) |
                                proc->bytecode[proc->ip + 3];
                proc->ip += 4;
                
                if(proc->sp < STACK_SIZE) {
                    proc->stack[proc->sp++] = (int32_t)value;
                    
                    // TODO: switch to core/kernel/log.h features
                    /* char dbg[64];
                    serial_print("DEBUG PUSH32: value=0x");
                    itoa(value, dbg, 16);
                    serial_print(dbg);
                    serial_print(" (");
                    itoa((int32_t)value, dbg, 10);
                    serial_print(dbg);
                    serial_print(") at ip=");
                    itoa(proc->ip, dbg, 10);
                    serial_print(dbg);
                    serial_print("\n"); */

                } else {
                    LOG_WARN("process %d: Stack overflow in PUSH32\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: Not enough bytes\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x04: // POP
            if(proc->sp > 0) {
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in POP\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x05: // DUP
            if(proc->sp == 0) {
                LOG_WARN("process %d: Stack underflow in DUP\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            if(proc->sp >= STACK_SIZE) {
                LOG_WARN("process %d: Stack overflow in DUP\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            
            proc->stack[proc->sp] = proc->stack[proc->sp - 1];
            proc->sp++;
            break;
        
        case 0x06: // SWAP
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                proc->stack[proc->sp - 2] = top;
                proc->stack[proc->sp - 1] = second;
            } else {
                LOG_WARN("process %d: Stack underflow in SWAP\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        // Arithmetic:
        case 0x10: // ADD
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result = top + second; 
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in ADD\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x11: // SUB
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result = second - top;
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in SUB\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x12: // MUL
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result = second * top;
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in MUL\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x13: // DIV
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result;

                if(top != 0) {
                    result = second / top;
                    proc->stack[proc->sp - 2] = result;
                    proc->sp--;
                } else {
                    LOG_WARN("process %d: Zero division DIV. Terminate process. \n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: Stack underflow in DIV\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x14: // MOD
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                
                if (top == 0) {
                    LOG_WARN("process %d: Zero division MOD. Terminate process. \n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }

                int32_t result = second % top;
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in MOD\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;
        
        // Comparisons:
        case 0x20: // CMP
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result;

                if(second < top) {
                    result = -1;
                } else if (top == second) {
                    result = 0;
                } else {
                    result = 1;
                }
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in CMP\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x21: // EQ
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result = (top == second) ? 1 : 0;
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in EQ\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x22: // NEQ
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result = (top != second) ? 1 : 0;
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in NEQ\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x23: // GT
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result = (second > top) ? 1 : 0;
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in GT\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x24: // LT
            if(proc->sp >= 2) {
                int32_t top = proc->stack[proc->sp - 1];
                int32_t second = proc->stack[proc->sp - 2];
                int32_t result = (second < top) ? 1 : 0;
                
                proc->stack[proc->sp - 2] = result;
                proc->sp--;
            } else {
                LOG_WARN("process %d: Stack underflow in LT\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        // Flow control (32-bit addresses):
        case 0x30: // JMP
            if(proc->ip + 3 < proc->size) {
                uint32_t addr = (proc->bytecode[proc->ip] << 24) |
                               (proc->bytecode[proc->ip + 1] << 16) |
                               (proc->bytecode[proc->ip + 2] << 8) |
                               proc->bytecode[proc->ip + 3];
                proc->ip += 4;
                
                if(addr >= 4 && addr < proc->size) {
                    proc->ip = addr;
                } else {
                    LOG_WARN("process %d: Invalid address for JMP32\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            }
            break;

        case 0x31: // JZ
            if (proc->sp > 0) {
                int32_t value = proc->stack[--proc->sp];
                if (proc->ip + 3 < proc->size) {
                    uint32_t addr = (proc->bytecode[proc->ip] << 24) |
                                   (proc->bytecode[proc->ip + 1] << 16) |
                                   (proc->bytecode[proc->ip + 2] << 8) |
                                   proc->bytecode[proc->ip + 3];
                    proc->ip += 4;
                    
                    if (value == 0) {
                        if (addr >= 4 && addr < proc->size) {
                            proc->ip = addr;
                        } else {
                            LOG_WARN("process %d: Invalid address for JZ32\n", proc->pid);
                            proc->exit_code = -1;
                            proc->active = false;
                            return false;
                        }
                    }
                } else {
                    LOG_WARN("process %d: Not enough bytes for address JZ32\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: Stack underflow in JZ32\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x32: // JNZ
            if (proc->sp > 0) {
                int32_t value = proc->stack[--proc->sp];
                if (proc->ip + 3 < proc->size) {
                    uint32_t addr = (proc->bytecode[proc->ip] << 24) |
                                   (proc->bytecode[proc->ip + 1] << 16) |
                                   (proc->bytecode[proc->ip + 2] << 8) |
                                   proc->bytecode[proc->ip + 3];
                    proc->ip += 4;
                    
                    if (value != 0) {
                        if (addr >= 4 && addr < proc->size) {
                            proc->ip = addr;
                        } else {
                            LOG_WARN("process %d: Invalid address for JNZ32\n", proc->pid);
                            proc->exit_code = -1;
                            proc->active = false;
                            return false;
                        }
                    }
                } else {
                    LOG_WARN("process %d: Not enough bytes for address JNZ32\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: Stack underflow in JNZ32\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x33: // CALL
            if(proc->ip + 3 < proc->size) {
                uint32_t addr = (proc->bytecode[proc->ip] << 24) |
                               (proc->bytecode[proc->ip + 1] << 16) |
                               (proc->bytecode[proc->ip + 2] << 8) |
                               proc->bytecode[proc->ip + 3];
                proc->ip += 4;
                
                if(proc->sp < STACK_SIZE - 1) {
                    proc->stack[proc->sp++] = proc->ip;
                    
                    if(addr >= 4 && addr < proc->size) {
                        proc->ip = addr;
                    } else {
                        LOG_WARN("process %d: Invalid address for CALLZ32\n", proc->pid);
                        proc->exit_code = -1;
                        proc->active = false;
                        return false;
                    }
                } else {
                    LOG_WARN("process %d: Stack overflow in CALL32\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: Not enough bytes for address CALL32\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x34: // RET
            if(proc->sp > 0) {
                uint32_t return_addr = (uint32_t)proc->stack[--proc->sp];
                
                if(return_addr >= 4 && return_addr < proc->size) {
                    proc->ip = return_addr;
                } else {
                    LOG_WARN("process %d: invalid return address\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: stack underflow in RET\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        // Memory:
        case 0x40: // LOAD
            if(proc->ip < proc->size) {
                uint8_t var_index = proc->bytecode[proc->ip++];
                
                if(var_index < MAX_LOCALS) {
                    int32_t value = proc->locals[var_index];
                    
                    if(proc->sp < STACK_SIZE) {
                        proc->stack[proc->sp++] = value;
                    } else {
                        LOG_WARN("process %d: Stack overflow in LOAD\n", proc->pid);
                        proc->exit_code = -1;
                        proc->active = false;
                        return false;
                    }
                } else {
                    LOG_WARN("process %d: invalid variable index in LOAD\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            }
            break;

        case 0x41: // STORE
            if(proc->ip < proc->size) {
                uint8_t var_index = proc->bytecode[proc->ip++];
                
                if(var_index < MAX_LOCALS && proc->sp > 0) {
                    int32_t value = proc->stack[--proc->sp];
                    proc->locals[var_index] = value;
                } else {
                    LOG_WARN("process %d: invalid index or stack underflow in STORE\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            }
            break;

        // Memory absolute access:
        case 0x44: // LOAD_ABS - load from absolute memory address
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                LOG_WARN("process %d: Required caps not receivedn\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }

            if(proc->sp > 0) {
                uint32_t addr = (uint32_t)proc->stack[proc->sp - 1]; // get address from stack

                if((addr >= 0x100000 && addr < 0xFFFFFFFF) || 
                (addr >= 0xB8000 && addr <= 0xB8FA0)) {
                    int32_t value = *(int32_t*)addr;
                    proc->stack[proc->sp - 1] = value;

                    // Debug TODO: switch to kernel/log.h features
                    // char dbg[64];
                    // serial_print("DEBUG LOAD_ABS: addr=0x");
                    // itoa(addr, dbg, 16);
                    // serial_print(dbg);
                    // serial_print(" -> value=0x");
                    // itoa(value, dbg, 16);
                    // serial_print(dbg);
                    // serial_print("\n");
                } else {
                    LOG_WARN("process %d: invalid memory address in LOAD_ABS\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: stack underflow in LOAD_ABS\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        case 0x45: // STORE_ABS - store to absolute memory address
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                LOG_WARN("process %d: Required caps not receivedn\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }

            if(proc->sp >= 2) {
                uint32_t addr = (uint32_t)proc->stack[proc->sp - 2]; // address
                int32_t value = proc->stack[proc->sp - 1]; // value

                if((addr >= 0x100000 && addr < 0xFFFFFFFF) || 
                (addr >= 0xB8000 && addr <= 0xB8FA0)) {
                    // Special handling for VGA text buffer - write only 16 bits (char + attribute)
                    if (addr >= 0xB8000 && addr <= 0xB8FA0) {
                        *(uint16_t*)addr = (uint16_t)(value & 0xFFFF);
                        
                        // Debug VGA writes
                        char dbg[64];
                        serial_print("VGA WRITE: addr=0x");
                        itoa(addr, dbg, 16);
                        serial_print(dbg);
                        serial_print(" <- value=0x");
                        itoa(value, dbg, 16);
                        serial_print(dbg);
                        serial_print(" (char='");
                        char ch[2] = {(char)(value & 0xFF), 0};
                        serial_print(ch);
                        serial_print("')\n");
                    } else {
                        *(int32_t*)addr = value;
                    }
                    proc->sp -= 2;
                } else {
                    LOG_WARN("process %d: Invalid memory address in STORE_ABS\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: Stack underflow in STORE_ABS\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;

        // System calls:
        case 0x51: // BREAK
            LOG_DEBUG("process %d: Stop from BREAK\n", proc->pid);
            char dbg[64];
            serial_print("IP: ");
            itoa(proc->ip, dbg, 10);
            serial_print(dbg);
            serial_print(", SP: ");
            itoa(proc->sp, dbg, 10);
            serial_print(dbg);
            serial_print("\n");
            break;
            
        case 0x50: // SYSCALL
            if(proc->ip < proc->size) {
                uint8_t syscall_id = proc->bytecode[proc->ip++];
                syscall_handler(syscall_id, proc);
            }
            break;
            
        default:
            char buffer[32];
            LOG_WARN("process %d: Unknown opcode: 0x%s", proc->pid, buffer);
            proc->exit_code = -1;
            proc->active = false;
            return false;
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
        // Execute multiple instructions per tick for better performance
        for(int i = 0; i < 100; i++) {
            if (processes[current_process].ip < processes[current_process].size && 
                processes[current_process].active && 
                !processes[current_process].blocked) {
                if(!nvm_execute_instruction(&processes[current_process])) {
                    break; // Stop if instruction returns false (halt, error, etc)
                }
            } else {
                if(processes[current_process].ip >= processes[current_process].size && 
                   processes[current_process].active) {
                    char buffer[32];
                    itoa(processes[current_process].pid, buffer, 10);

                    LOG_WARN("process %s: Reached end of code - terminating\n", buffer);
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
