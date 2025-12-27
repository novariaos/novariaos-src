#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include <stdint.h>

#define IDT_SIZE 256
#define INTERRUPT_GATE 0x8e
#define KERNEL_CODE_SEGMENT_OFFSET 0x08
#define KERNEL_CS 0x08

#define ENTER_KEY_CODE 0x1C
#define MAX_TEXT_SIZE 1024

#define SYSCALL_INTERRUPT 0x80

extern uint8_t inb(uint16_t port);

#endif
