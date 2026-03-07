// SPDX-License-Identifier: GPL-3.0-only

#include <core/arch/cpuid.h>

void cpuid(uint32_t leaf, uint32_t subleaf, cpuid_result_t* result) {
    asm volatile(
        "mov %4, %%ecx\n\t"
        "cpuid\n\t"
        "mov %%eax, %0\n\t"
        "mov %%ebx, %1\n\t"
        "mov %%ecx, %2\n\t"
        "mov %%edx, %3\n\t"
        : "=m"(result->eax), "=m"(result->ebx), "=m"(result->ecx), "=m"(result->edx)
        : "r"(subleaf), "a"(leaf)
        : "%ebx", "%ecx", "%edx"
    );
}
