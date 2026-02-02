// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/instructions.h>
#include <core/kernel/log.h>

bool handle_add(nvm_process_t* proc) {
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
    return true;
}

bool handle_sub(nvm_process_t* proc) {
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
    return true;
}

bool handle_mul(nvm_process_t* proc) {
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
    return true;
}

bool handle_div(nvm_process_t* proc) {
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
    return true;
}

bool handle_mod(nvm_process_t* proc) {
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
    return true;
}

bool handle_cmp(nvm_process_t* proc) {
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
    return true;
}

bool handle_eq(nvm_process_t* proc) {
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
    return true;
}

bool handle_neq(nvm_process_t* proc) {
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
    return true;
}

bool handle_gt(nvm_process_t* proc) {
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
    return true;
}

bool handle_lt(nvm_process_t* proc) {
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
    return true;
}