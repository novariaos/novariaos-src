// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/instructions.h>
#include <core/kernel/log.h>
#include <core/drivers/serial.h>

bool handle_halt(nvm_process_t* proc) {
    proc->active = false;
    proc->exit_code = 0;
    LOG_DEBUG("process %d: Halted\n", proc->pid);
    return false;
}

bool handle_nop(nvm_process_t* proc) {
    return true;
}

bool handle_push(nvm_process_t* proc) {
    if(proc->ip + 3 < proc->size) {
        uint32_t value = (proc->bytecode[proc->ip] << 24) |
                        (proc->bytecode[proc->ip + 1] << 16) |
                        (proc->bytecode[proc->ip + 2] << 8) |
                        proc->bytecode[proc->ip + 3];
        proc->ip += 4;
        
        if(proc->sp < STACK_SIZE) {
            proc->stack[proc->sp++] = (int32_t)value;
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
    return true;
}

bool handle_pop(nvm_process_t* proc) {
    if(proc->sp > 0) {
        proc->sp--;
    } else {
        LOG_WARN("process %d: Stack underflow in POP\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    return true;
}

bool handle_dup(nvm_process_t* proc) {
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
    return true;
}

bool handle_swap(nvm_process_t* proc) {
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
    return true;
}

bool handle_enter(nvm_process_t* proc) {
    if (proc->ip < proc->size) {
        uint8_t locals = proc->bytecode[proc->ip++];
        if (proc->sp + 1 + locals <= STACK_SIZE) {
            proc->stack[proc->sp++] = proc->fp;
            proc->fp = proc->sp - 1;
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
    return true;
}

bool handle_leave(nvm_process_t* proc) {
    if (proc->fp < 0 || proc->fp >= STACK_SIZE) {
        LOG_WARN("process %d: Invalid frame pointer in LEAVE\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    if (proc->sp < proc->fp + 1) {
        LOG_WARN("process %d: Corrupted stack/frame in LEAVE\n", proc->pid);
        proc->exit_code = -1;
        proc->active = false;
        return false;
    }
    
    proc->sp = proc->fp + 1;
    int32_t saved_fp = proc->stack[proc->fp];
    proc->sp = proc->fp;
    proc->fp = saved_fp;
    return true;
}