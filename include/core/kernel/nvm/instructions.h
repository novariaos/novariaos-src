// SPDX-License-Identifier: GPL-3.0-only

#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include <core/kernel/nvm/nvm.h>
#include <stdbool.h>

typedef bool (*instruction_handler_t)(nvm_process_t*);

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

// Flow control
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
bool handle_load_heap(nvm_process_t* proc);
bool handle_store_heap(nvm_process_t* proc);

// System
bool handle_syscall(nvm_process_t* proc);
bool handle_break(nvm_process_t* proc);

// Bitwise operations
bool handle_and(nvm_process_t* proc);
bool handle_or(nvm_process_t* proc);
bool handle_xor(nvm_process_t* proc);
bool handle_not(nvm_process_t* proc);
bool handle_shl(nvm_process_t* proc);
bool handle_shr(nvm_process_t* proc);
bool handle_sar(nvm_process_t* proc);

#endif