#ifndef ARCH_PANIC_H
#define ARCH_PANIC_H

#include <core/kernel/kstd.h>

inline static void panic(const char* message) {
    asm volatile ("cli");
    kprint("KERNEL PANIC: ", 4);
    kprint(message, 4);
    kprint("\n", 4);

    for (;;) {
        asm volatile("hlt");
    }
}

#endif // ARCH_PANIC_H
