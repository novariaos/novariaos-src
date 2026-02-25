// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/instructions.h>
#include <log.h>

bool handle_jmp(nvm_process_t* proc) {
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
    return true;
}

bool handle_jz(nvm_process_t* proc) {
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
    return true;
}

bool handle_jnz(nvm_process_t* proc) {
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
    return true;
}

bool handle_call(nvm_process_t* proc) {
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
                LOG_WARN("process %d: Invalid address for CALL32\n", proc->pid);
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
    return true;
}

bool handle_ret(nvm_process_t* proc) {
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
    return true;
}

bool handle_load_arg(nvm_process_t* proc) {
    if (proc->ip < proc->size) {
        uint8_t off = proc->bytecode[proc->ip++];
        if (proc->fp <= 0) {
            LOG_WARN("process %d: LOAD_ARG without valid frame\n", proc->pid);
            proc->exit_code = -1;
            proc->active = false;
            return false;
        }
        int32_t idx = (proc->fp - 2) - (int32_t)off;
        if (idx >= 0 && idx < proc->sp && proc->sp < STACK_SIZE) {
            proc->stack[proc->sp++] = proc->stack[idx];
        }
    }
    return true;
}

bool handle_store_arg(nvm_process_t* proc) {
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
    return true;
}