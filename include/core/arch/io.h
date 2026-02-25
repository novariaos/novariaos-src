#ifndef ARCH_IO_H
#define ARCH_IO_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t res;
    __asm__("inb %w1, %b0" : "=a"(res) : "Nd"(port) : "memory");
    return res;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint16_t inw(uint16_t port) {
    uint16_t res;
    __asm__("inw %w1, %w0" : "=a"(res) : "Nd"(port) : "memory");
    return res;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__("outw %w0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

#endif
