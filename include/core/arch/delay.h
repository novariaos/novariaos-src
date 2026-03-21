#ifndef DELAY_H
#define DELAY_H

#include <arch/pause.h>
#include <stdint.h>

#define IO_DELAY_PORT   0x80

static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    asm volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static inline uint64_t rdtsc_serialized(void) {
    uint32_t low, high;
    asm volatile (
        "cpuid\n"
        "rdtsc\n"
        : "=a"(low), "=d"(high)
        : "a"(0)
        : "ebx", "ecx"
    );
    return ((uint64_t)high << 32) | low;
}

static inline void io_delay(void) {
    asm volatile ("outb %%al, $0x80" : : "a"(0));
}

static inline void io_delay_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;
    asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
}

static inline void udelay(uint64_t microseconds, uint64_t tsc_freq_hz) {
    uint64_t tsc_start = rdtsc();
    uint64_t tsc_ticks_needed = (microseconds * tsc_freq_hz) / 1000000ULL;
    uint64_t tsc_end = tsc_start + tsc_ticks_needed;
    
    while (rdtsc() < tsc_end) {
        cpu_relax();
    }
}

static inline void mdelay(uint64_t milliseconds, uint64_t tsc_freq_hz) {
    udelay(milliseconds * 1000, tsc_freq_hz);
}

static inline void busy_delay(uint32_t loops) {
    for (uint32_t i = 0; i < loops; i++) {
        cpu_relax();
    }
}

#endif // DELAY_H