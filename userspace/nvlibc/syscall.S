.section .text
    .globl nv_exit
nv_exit:
    li a7, 0
    ecall
1:
    j 1b

    .globl nv_tty_write
nv_tty_write:
    li a7, 1
    ecall
    ret