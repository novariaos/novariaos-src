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

.globl nv_spawn
nv_spawn:
    li a7, 2
    ecall
    ret

.globl nv_sleep
nv_sleep:
    li a7, 3
    ecall
    ret

.globl nv_open
nv_open:
    li a7, 16
    ecall
    ret

.globl nv_close
nv_close:
    li a7, 17
    ecall
    ret