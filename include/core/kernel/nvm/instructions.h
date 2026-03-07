#ifndef NVM_INSTRUCTION_H
#define NVM_INSTRUCTION_H

#include <core/kernel/nvm/nvm.h>

// Stack operations
bool handle_halt(nvm_process_t* proc);
bool handle_nop(nvm_process_t* proc);
bool handle_push(nvm_process_t* proc);
bool handle_pop(nvm_process_t* proc);
bool handle_dup(nvm_process_t* proc);
bool handle_swap(nvm_process_t* proc);
bool handle_enter(nvm_process_t* proc);
bool handle_leave(nvm_process_t* proc);

// Arithmetic operations
bool handle_add(nvm_process_t* proc);
bool handle_sub(nvm_process_t* proc);
bool handle_mul(nvm_process_t* proc);
bool handle_div(nvm_process_t* proc);
bool handle_mod(nvm_process_t* proc);

// Comparison operations
bool handle_cmp(nvm_process_t* proc);
bool handle_eq(nvm_process_t* proc);
bool handle_neq(nvm_process_t* proc);
bool handle_gt(nvm_process_t* proc);
bool handle_lt(nvm_process_t* proc);

// Flow control operations
bool handle_jmp(nvm_process_t* proc);
bool handle_jz(nvm_process_t* proc);
bool handle_jnz(nvm_process_t* proc);
bool handle_call(nvm_process_t* proc);
bool handle_ret(nvm_process_t* proc);
bool handle_load_arg(nvm_process_t* proc);
bool handle_store_arg(nvm_process_t* proc);

// Memory operations
bool handle_load(nvm_process_t* proc);
bool handle_store(nvm_process_t* proc);
bool handle_load_rel(nvm_process_t* proc);
bool handle_store_rel(nvm_process_t* proc);
bool handle_load_abs(nvm_process_t* proc);
bool handle_store_abs(nvm_process_t* proc);

// System operations
bool handle_syscall(nvm_process_t* proc);
bool handle_break(nvm_process_t* proc);

#endif // NVM_INSTRUCTION_H