.section .text
    .globl _start
_start:
    call _clear_bss
    call main
    call exit
1:
    j 1b

    .globl _clear_bss
_clear_bss:
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    ret