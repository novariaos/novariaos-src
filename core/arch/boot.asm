; SPDX-License-Identifier: GPL-3.0-only

section .text
bits 64

global _start
extern kmain

section .bss
align 16
stack_bottom:
    resb 16384  ; 16 KB stack
stack_top:

section .text
_start:
    mov rsp, stack_top

    cld
    push rdi

    call kmain
    
    cli
.hang:
    hlt
    jmp .hang
