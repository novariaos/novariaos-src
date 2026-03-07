// SPDX-License-Identifier: GPL-3.0-only

#include <core/arch/pause.h>
#include <stdint.h>

static int has_rdrand(void) {
    int32_t ecx = 0;
    asm volatile(
        "mov $1, %%eax\n\t"
        "cpuid\n\t"
        : "=c"(ecx)
        : 
        : "%eax", "%ebx", "%edx"
    );
    return (ecx >> 30) & 1;  // CPUID.01H:ECX.RDRAND[bit 30]
}

uint64_t get_hw_entropy(void) {
    uint64_t result = 0;

    if(has_rdrand()) {
        for(int attempts = 0; attempts < 10; attempts++) {
            int8_t success = 0;
            int32_t low = 0, high = 0;

            asm volatile(
                "rdrand %%eax\n\t"
                "setc %1\n\t"
                : "=a"(low), "=r"(success)
                :
                : "cc"
            );
            
            if(success) {
                asm volatile(
                    "rdrand %%eax\n\t"
                    "setc %1\n\t"
                    : "=a"(high), "=r"(success)
                    :
                    : "cc"
                );
                
                if(success) {
                    result = ((uint64_t)high << 32) | low;
                    return result;
                }
            }

            cpu_relax();
        }
    }
    
    // Fallback:
    int32_t tsc_low, tsc_high;
    int32_t stack_addr;
    
    asm volatile(
        "rdtsc\n\t"
        : "=a"(tsc_low), "=d"(tsc_high)
    );
    
    asm volatile(
        "movl %%esp, %0\n\t"
        : "=r"(stack_addr)
    );
    
    result = ((uint64_t)tsc_high << 32) | tsc_low;
    result ^= (uint64_t)stack_addr;
    result ^= (uint64_t)&result;
    
    result = (result << 13) | (result >> 51);
    
    return result ? result : 0xDEADBEEFCAFEBABE;
}