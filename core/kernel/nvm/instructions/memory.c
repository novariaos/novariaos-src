// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/instructions.h>
#include <core/kernel/nvm/caps.h>
#include <log.h>

bool handle_load(nvm_process_t* proc) {
    if(proc->ip < proc->size) {
        uint8_t var_index = proc->bytecode[proc->ip++];
        
        if(var_index < MAX_LOCALS) {
            int32_t value = proc->locals[var_index];
            
            if(proc->sp < STACK_SIZE) {
                proc->stack[proc->sp++] = value;
            }
        }
    }
    return true;
}

bool handle_store(nvm_process_t* proc) {
    if(proc->ip < proc->size) {
        uint8_t var_index = proc->bytecode[proc->ip++];
        
        if(var_index < MAX_LOCALS && proc->sp > 0) {
            int32_t value = proc->stack[--proc->sp];
            proc->locals[var_index] = value;
        }
    }
    return true;
}

bool handle_load_rel(nvm_process_t* proc) {
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
            return false;
        }
    }
    return true;
}

bool handle_store_rel(nvm_process_t* proc) {
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
    return true;
}

bool handle_load_abs(nvm_process_t* proc) {
    if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
        LOG_WARN("process %d: Required caps not received\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }

    if(proc->sp > 0) {
        uintptr_t addr = (uintptr_t)proc->stack[proc->sp - 1];
        
        if((addr >= 0x100000 && addr < 0xFFFFFFFF) || 
        (addr >= 0xB8000 && addr <= 0xB8FA0)) {
            int32_t value = *(int32_t*)addr;
            proc->stack[proc->sp - 1] = value;
        }
    }
    return true;
}

bool handle_store_abs(nvm_process_t* proc) {
    if (!caps_has_capability(proc, CAP_DRV_ACCESS)) {
        LOG_WARN("process %d: Required caps not received\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }

    if(proc->sp >= 2) {
        uintptr_t addr = (uintptr_t)proc->stack[proc->sp - 2];
        int32_t value = proc->stack[proc->sp - 1];

        if((addr >= 0x100000 && addr < 0xFFFFFFFF) || 
        (addr >= 0xB8000 && addr <= 0xB8FA0)) {
            if (addr >= 0xB8000 && addr <= 0xB8FA0) {
                *(uint16_t*)addr = (uint16_t)(value & 0xFFFF);
            } else {
                *(int32_t*)addr = value;
            }
            proc->sp -= 2;
        }
    }
    return true;
}