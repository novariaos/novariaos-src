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
        processes[i].fp = -1;
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

        // Stack frames:
        case 0x35: { // ENTER locals_count (uint8)
            if (proc->ip < proc->size) {
                uint8_t locals = proc->bytecode[proc->ip++];
                // Need space: save fp (1) + locals
                if (proc->sp + 1 + locals <= STACK_SIZE) {
                    // push old fp
                    proc->stack[proc->sp++] = proc->fp;
                    // set new fp to index of saved fp
                    proc->fp = proc->sp - 1;
                    // allocate locals (initialize to 0)
                    for (uint8_t i = 0; i < locals; i++) {
                        proc->stack[proc->sp++] = 0;
                    }
                } else {
                    LOG_WARN("process %d: Stack overflow in ENTER\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
            } else {
                LOG_WARN("process %d: Not enough bytes for ENTER\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            break;
        }
        case 0x36: { // LEAVE
            if (proc->fp < 0 || proc->fp >= STACK_SIZE) {
                LOG_WARN("process %d: Invalid frame pointer in LEAVE\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            // locals are any items above saved fp index
            if (proc->sp < proc->fp + 1) {
                LOG_WARN("process %d: Corrupted stack/frame in LEAVE\n", proc->pid);
                proc->exit_code = -1;
                proc->active = false;
                return false;
            }
            // Drop locals
            proc->sp = proc->fp + 1;
            // Restore old fp
            int32_t saved_fp = proc->stack[proc->fp];
            // Pop saved fp
            proc->sp = proc->fp; // remove saved fp slot
            // Now restore fp value
            proc->fp = saved_fp;
            break;
        }
        case 0x42: { // LOAD_REL offset
            if (proc->ip < proc->size) {
                uint8_t off = proc->bytecode[proc->ip++];
                if (proc->fp < 0) {
                    LOG_WARN("process %d: LOAD_REL without frame\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
                int32_t idx = proc->fp + 1 + off;
                if (idx >= 0 && idx < proc->sp && proc->sp < STACK_SIZE) {
                    proc->stack[proc->sp++] = proc->stack[idx];
                } else {
                    LOG_WARN("process %d: Invalid index in LOAD_REL\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                }
            }
            break;
        }
        case 0x43: { // STORE_REL offset
            if (proc->ip < proc->size) {
                uint8_t off = proc->bytecode[proc->ip++];
                if (proc->fp < 0) {
                    LOG_WARN("process %d: STORE_REL without frame\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
                int32_t idx = proc->fp + 1 + off;
                if (idx >= 0 && idx < STACK_SIZE && proc->sp > 0) {
                    int32_t value = proc->stack[--proc->sp];
                    proc->stack[idx] = value;
                }
            }
            break;
        }

        // Arguments access:
        case 0x37: { // LOAD_ARG offset
            if (proc->ip < proc->size) {
                uint8_t off = proc->bytecode[proc->ip++];
                if (proc->fp <= 0) { // need at least return_addr at fp-1
                    LOG_WARN("process %d: LOAD_ARG without valid frame\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
                int32_t idx = (proc->fp - 2) - (int32_t)off; // arg0 at fp-2, arg1 at fp-3, ...
                if (idx >= 0 && idx < proc->sp && proc->sp < STACK_SIZE) {
                    proc->stack[proc->sp++] = proc->stack[idx];
                }
            }
            break;
        }
        case 0x38: { // STORE_ARG offset
            if (proc->ip < proc->size) {
                uint8_t off = proc->bytecode[proc->ip++];
                if (proc->fp <= 0) {
                    LOG_WARN("process %d: STORE_ARG without valid frame\n", proc->pid);
                    proc->exit_code = -1;
                    proc->active = false;
                    return false;
                }
                int32_t idx = (proc->fp - 2) - (int32_t)off;
                if (idx >= 0 && idx < STACK_SIZE && proc->sp > 0) {
                    int32_t value = proc->stack[--proc->sp];
                    proc->stack[idx] = value;
                }
            }
            break;
        }

        // Memory:
        case 0x40: // LOAD
            if(proc->ip < proc->size) {
                uint8_t var_index = proc->bytecode[proc->ip++];
                
                if(var_index < MAX_LOCALS) {
                    int32_t value = proc->locals[var_index];
                    
                    if(proc->sp < STACK_SIZE) {
                        proc->stack[proc->sp++] = value;
                    }
                }
            }
            break;

        case 0x41: // STORE
            if(proc->ip < proc->size) {
                uint8_t var_index = proc->bytecode[proc->ip++];
                
                if(var_index < MAX_LOCALS && proc->sp > 0) {
                    int32_t value = proc->stack[--proc->sp];
                    proc->locals[var_index] = value;
                }
            }
            break;

        // Memory absolute access:
        case 0x44: // LOAD_ABS - load from absolute memory address
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                LOG_WARN("process %d: Required caps not received\n", proc->pid);
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
                }
            }
            break;

        case 0x45: // STORE_ABS - store to absolute memory address
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                LOG_WARN("process %d: Required caps not received\n", proc->pid);
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
                    } else {
                        *(int32_t*)addr = value;
                    }
                    proc->sp -= 2;
                }
            }
            break;

        // System calls:
        case 0x51: // BREAK
            LOG_DEBUG("process %d: Stop from BREAK\n", proc->pid);
            break;
            
        case 0x50: // SYSCALL
            if(proc->ip < proc->size) {
                uint8_t syscall_id = proc->bytecode[proc->ip++];
                syscall_handler(syscall_id, proc);
            }
            break;
            
        default:
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

#include <stddef.h>
#include <string.h>
#include <core/kernel/mem.h>
#include <core/fs/vfs.h>
#include <core/arch/io.h>

typedef struct {
    uint16_t recipient;
    uint16_t sender;
    uint8_t content;
} message_t;

#define MAX_MESSAGES 32
static message_t message_queue[MAX_MESSAGES];
static int message_count = 0;

int32_t syscall_handler(uint8_t syscall_id, nvm_process_t* proc) {
    int32_t result = 0;
    
    switch(syscall_id) {
        case SYS_EXIT: {
            if(proc->sp >= 1) {
                proc->exit_code = proc->stack[proc->sp - 1];
            } else {
                proc->exit_code = 0;
            }
            proc->active = false;

            procfs_unregister(proc->pid);
            if(proc->sp > 0) proc->sp--;
            break;
        }

        case SYS_SPAWN: {
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -1;
                break;
            }
            if (proc->sp < 1) {
                result = -1;
                break;
            }

            int target_fd = proc->stack[proc->sp - 1];
            int argc = proc->stack[proc->sp - 2];
            
            proc->sp -= 2;

            char* argv[argc];
            int arg_index = 0;
            int stack_pos = proc->sp - 1;
            
            while (arg_index < argc && stack_pos >= 0) {
                int end_pos = stack_pos;
                int start_pos = -1;

                while (stack_pos >= 0) {
                    if (proc->stack[stack_pos] == 0) {
                        start_pos = stack_pos + 1;
                        break;
                    }
                    stack_pos--;
                }
                
                if (start_pos == -1 || start_pos > end_pos) {
                    result = -1;
                    break;
                }

                int len = end_pos - start_pos + 1;
                argv[arg_index] = kmalloc(len + 1);
                if (!argv[arg_index]) {
                    result = -1;
                    break;
                }

                for (int i = 0; i < len; i++) {
                    argv[arg_index][i] = (char)proc->stack[start_pos + i];
                }
                argv[arg_index][len] = '\0';
                
                arg_index++;
                stack_pos = start_pos - 2;
            }
            
            if (result == -1) {
                for (int i = 0; i < arg_index; i++) {
                    kfree(argv[i]);
                }
                break;
            }

            proc->sp = stack_pos + 1;
            
            uint8_t* bytecode = NULL;
            size_t bytecode_size = 0;
            size_t allocated_size = 1024;

            bytecode = kmalloc(allocated_size);
            if (!bytecode) {
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                }
                result = -1;
                break;
            }

            while (1) {
                uint8_t read_byte;
                size_t bytes_read = vfs_readfd(target_fd, &read_byte, 1);
                
                if (bytes_read != 1) {
                    break;
                }

                if (bytecode_size >= allocated_size) {
                    allocated_size *= 2;
                    uint8_t* new_bytecode = kmalloc(allocated_size);
                    if (!new_bytecode) {
                        kfree(bytecode);
                        for (int i = 0; i < argc; i++) {
                            kfree(argv[i]);
                        }
                        result = -1;
                        break;
                    }

                    for (size_t i = 0; i < bytecode_size; i++) {
                        new_bytecode[i] = bytecode[i];
                    }
                    
                    kfree(bytecode);
                    bytecode = new_bytecode;
                }
                
                bytecode[bytecode_size] = read_byte;
                bytecode_size++;
            }
            
            if (result == -1) {
                break;
            }

            int total_string_len = 0;
            for (int i = 0; i < argc; i++) {
                total_string_len += strlen(argv[i]) + 1;
            }

            int stack_size = 1 + argc + total_string_len;
            int32_t* initial_stack = kmalloc(stack_size * sizeof(int32_t));
            if (!initial_stack) {
                kfree(bytecode);
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                }
                result = -1;
                break;
            }

            stack_pos = 0;
            initial_stack[stack_pos++] = argc;

            int argv_pointers_start = stack_pos;
            stack_pos += argc;

            for (int i = 0; i < argc; i++) {
                initial_stack[argv_pointers_start + i] = stack_pos;
                
                char* arg = argv[i];
                for (int j = 0; arg[j] != '\0'; j++) {
                    initial_stack[stack_pos++] = (int32_t)(uint8_t)arg[j];
                }
                initial_stack[stack_pos++] = 0;
            }

            int new_pid = nvm_create_process_with_stack(bytecode, bytecode_size,
                                                    (uint16_t[]){CAPS_NONE}, 1,
                                                    initial_stack, stack_pos);
            kfree(initial_stack);
            caps_copy(nvm_get_process(proc->pid), nvm_get_process(new_pid));

            if (new_pid < 0) {
                kfree(bytecode);
                for (int i = 0; i < argc; i++) {
                    kfree(argv[i]);
                }
                result = -1;
                break;
            }

            // kfree(bytecode); - УДАЛЕНО ДЛЯ ИСПРАВЛЕНИЯ USE-AFTER-FREE

            for (int i = 0; i < argc; i++) {
                kfree(argv[i]);
            }

            result = new_pid;
            break;
        }

        case SYS_OPEN: {
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -1;
                break;
            }

            if (proc->sp < 1) {
                result = -1;
                break;
            }

            int start_pos = proc->sp;
            int null_pos = -1;
            
            for (int i = proc->sp - 1; i >= 0; i--) {
                if ((proc->stack[i] & 0xFF) == 0) {
                    null_pos = i;
                    break;
                }
            }
            
            if (null_pos == -1) {
                result = -1;
                break;
            }
            
            char filename[MAX_FILENAME];
            int pos = 0;
       
            for (int i = start_pos - 1; i > null_pos && pos < MAX_FILENAME - 1; i--) {
                char ch = proc->stack[i] & 0xFF;
                filename[pos++] = ch;
            }
            
            filename[pos] = '\0';

            proc->sp = null_pos;
            
            int fd = vfs_open(filename, VFS_READ | VFS_WRITE);
            
            proc->stack[proc->sp] = fd;
            proc->sp++;
            
            result = 0;
            break;
        }

        case SYS_READ: {
            if (!caps_has_capability(proc, CAP_FS_READ)) {
                result = -1;
                break;
            }

            if (proc->sp < 1) {
                result = -1;
                break;
            }
            
            int32_t fd = proc->stack[proc->sp - 1];
            proc->sp--;
            
            if (fd < 0) {
                result = -1;
            } else {
                char buffer;
                size_t bytes = vfs_readfd(fd, &buffer, 1);
                
                if (bytes == 1) {
                    result = (unsigned char)buffer;
                } else if (bytes == 0) {
                    result = 0;  // EOF
                } else {
                    result = -1;
                }
            }

            proc->stack[proc->sp] = result;
            proc->sp++;
            break;
        }

        case SYS_WRITE: {
            if (!caps_has_capability(proc, CAP_FS_WRITE)) {
                result = -1;
                break;
            }

            if (proc->sp < 2) {
                result = -1;
                break;
            }
            
            int32_t fd = proc->stack[proc->sp - 2];
            int32_t byte_val = proc->stack[proc->sp - 1];
            proc->sp -= 2;
            
            if (fd < 0) {
                result = -1;
            } else if (fd == 1 || fd == 2) {
                char ch = (char)(byte_val & 0xFF);
                char str[2] = {ch, '\0'};
                kprint(str, 15);
                result = 1;
            } else {
                char ch = (char)(byte_val & 0xFF);
                result = vfs_writefd(fd, &ch, 1);
            }
            
            proc->stack[proc->sp] = result;
            proc->sp++;
            break;
        }

        case SYS_MSG_SEND: {
                if (proc->sp < 2) {
                    result = -1;
                    break;
                }

                uint16_t recipient = proc->stack[proc->sp - 2] & 0xFFFF;
                uint8_t content = proc->stack[proc->sp - 1] & 0xFF;

                if (message_count >= MAX_MESSAGES) {
                    result = -1;
                    break;
                }

                message_t msg;
                msg.recipient = recipient;
                msg.sender = proc->pid;
                msg.content = content;
                
                message_queue[message_count] = msg;
                message_count++;

                for (int i = 0; i < MAX_PROCESSES; i++) {
                    if (processes[i].active && processes[i].pid == recipient && processes[i].blocked) {
                        processes[i].blocked = false; 
                        processes[i].wakeup_reason = 1;
                        break;
                    }
                }

                proc->sp -= 2;
                break;
            }

        case SYS_MSG_RECEIVE: {
            int found_index = -1;
            for (int i = 0; i < message_count; i++) {
                if (message_queue[i].recipient == proc->pid) {
                    found_index = i;
                    break;
                }
            }
            
            if (found_index == -1) {
                proc->blocked = true;
                result = -1;
                break;
            }
            
            message_t received_msg = message_queue[found_index];
            
            for (int i = found_index; i < message_count - 1; i++) {
                message_queue[i] = message_queue[i + 1];
            }
            message_count--;

            if (proc->sp + 1 < STACK_SIZE) {
                proc->stack[proc->sp] = received_msg.sender;
                proc->stack[proc->sp + 1] = received_msg.content;
                proc->sp += 2;
            } else {
                result = -1;
                break;
            }
            break;
        }

        case SYS_PORT_IN_BYTE: {
            if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
                result = -1;
                break;
            }
            
            if (proc->sp == 0) {
                result = -1;
                break;
            }
            uint16_t port = proc->stack[proc->sp - 1];
            uint8_t val = inb(port);
            
            proc->stack[proc->sp - 1] = (int16_t)val;
            break;
        }

        case SYS_PORT_OUT_BYTE: {
            if (proc->sp < 2) {
                result = -1;
                break;
            }
            
            uint16_t port = proc->stack[proc->sp - 2] & 0xFFFF;
            uint8_t val = proc->stack[proc->sp - 1] & 0xFF;

            outb(port, val);
            proc->sp -= 2;
            break;
        }

        case SYS_PRINT: {
            if (proc->sp < 1) {
                result = -1;
                break;
            }

            uint8_t val = proc->stack[proc->sp - 1] & 0xFF;
            char print_char[2] = {(char)val, 0};
            kprint(print_char, 15);
            proc->sp -= 1;
            break;
        }

        default: {
            proc->exit_code = -1;
            proc->active = false;
        }
    }
    
    return result;
}
