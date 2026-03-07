#ifndef SYSCALL_H
#define SYSCALL_H

#include <core/kernel/nvm/nvm.h>
#include <stdint.h>

#define SYS_EXIT            0x00
#define SYS_SPAWN           0x01
#define SYS_OPEN            0x02
#define SYS_READ            0x03
#define SYS_WRITE           0x04
#define SYS_CREATE          0x05
#define SYS_DELETE          0x06
#define SYS_CAP_REQUEST     0x07
#define SYS_CAP_SPAWN       0x08
#define SYS_DRV_CALL        0x09
#define SYS_MSG_SEND        0x0A
#define SYS_MSG_RECEIVE     0x0B
#define SYS_PORT_IN_BYTE    0x0C
#define SYS_PORT_OUT_BYTE   0x0D
#define SYS_PRINT           0x0E

int32_t syscall_handler(uint8_t syscall_id, nvm_process_t* proc);

#endif