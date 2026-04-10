// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/instructions.h>
#include <log.h>

bool handle_and(nvm_process_t* proc) {
    if(proc->sp >= 2) {
        int32_t top = proc->stack[proc->sp - 1];
        int32_t second = proc->stack[proc->sp - 2];
        int32_t result = second & top;
        
        proc->stack[proc->sp - 2] = result;
        proc->sp--;
    } else {
        LOG_WARN("process %d: Stack underflow in AND\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}

bool handle_or(nvm_process_t* proc) {
    if(proc->sp >= 2) {
        int32_t top = proc->stack[proc->sp - 1];
        int32_t second = proc->stack[proc->sp - 2];
        int32_t result = second | top;
        
        proc->stack[proc->sp - 2] = result;
        proc->sp--;
    } else {
        LOG_WARN("process %d: Stack underflow in OR\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}

bool handle_xor(nvm_process_t* proc) {
    if(proc->sp >= 2) {
        int32_t top = proc->stack[proc->sp - 1];
        int32_t second = proc->stack[proc->sp - 2];
        int32_t result = second ^ top;
        
        proc->stack[proc->sp - 2] = result;
        proc->sp--;
    } else {
        LOG_WARN("process %d: Stack underflow in XOR\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}

bool handle_not(nvm_process_t* proc) {
    if(proc->sp >= 1) {
        int32_t top = proc->stack[proc->sp - 1];
        int32_t result = ~top;
        
        proc->stack[proc->sp - 1] = result;
    } else {
        LOG_WARN("process %d: Stack underflow in NOT\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}

bool handle_shl(nvm_process_t* proc) {
    if(proc->sp >= 2) {
        int32_t top = proc->stack[proc->sp - 1];
        int32_t second = proc->stack[proc->sp - 2];
        uint32_t shift = top & 31;
        int32_t result;
        
        if(shift) {
            result = (int32_t)((uint32_t)second << shift);
        } else {
            result = second;
        }
        
        proc->stack[proc->sp - 2] = result;
        proc->sp--;
    } else {
        LOG_WARN("process %d: Stack underflow in SHL\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}

bool handle_shr(nvm_process_t* proc) {
    if(proc->sp >= 2) {
        int32_t top = proc->stack[proc->sp - 1];
        int32_t second = proc->stack[proc->sp - 2];
        uint32_t shift = top & 31;
        int32_t result;
        
        if(shift) {
            result = (uint32_t)second >> shift;
        } else {
            result = second;
        }
        
        proc->stack[proc->sp - 2] = result;
        proc->sp--;
    } else {
        LOG_WARN("process %d: Stack underflow in SHR\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}

bool handle_sar(nvm_process_t* proc) {
    if(proc->sp >= 2) {
        int32_t top = proc->stack[proc->sp - 1];
        int32_t second = proc->stack[proc->sp - 2];
        uint32_t shift = top & 31;
        int32_t result;
        
        if(shift) {
            result = second >> shift;
        } else {
            result = second;
        }
        
        proc->stack[proc->sp - 2] = result;
        proc->sp--;
    } else {
        LOG_WARN("process %d: Stack underflow in SAR\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}