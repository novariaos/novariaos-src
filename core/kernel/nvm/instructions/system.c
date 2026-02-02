#include <core/kernel/nvm/instructions.h>
#include <core/kernel/nvm/syscall.h>
#include <core/kernel/log.h>

bool handle_syscall(nvm_process_t* proc) {
    if(proc->ip < proc->size) {
        uint8_t syscall_id = proc->bytecode[proc->ip++];
        syscall_handler(syscall_id, proc);
    }
    return true;
}

bool handle_break(nvm_process_t* proc) {
    LOG_DEBUG("process %d: Stop from BREAK\n", proc->pid);
    return true;
}