#ifndef ARCH_PANIC_H
#define ARCH_PANIC_H

#include <core/kernel/kstd.h>
#include <core/arch/idt.h>
#include <stdint.h>

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
} cpu_regs_t;

void panic_fb_init(uint32_t *addr, uint32_t w, uint32_t h, uint32_t pitch_px);
void panic(const char* message) __attribute__((noreturn));
void exception_halt(const char* name, interrupt_frame_t* frame,
                    uint64_t error_code, int has_err,
                    const cpu_regs_t* regs) __attribute__((noreturn));

#define CAPTURE_REGS(r) \
    __asm__ volatile( \
        "movq %%rax, %[rax]\n" \
        "movq %%rbx, %[rbx]\n" \
        "movq %%rcx, %[rcx]\n" \
        "movq %%rdx, %[rdx]\n" \
        "movq %%rsi, %[rsi]\n" \
        "movq %%rdi, %[rdi]\n" \
        "movq %%rbp, %[rbp]\n" \
        "movq %%r8,  %[r8] \n" \
        "movq %%r9,  %[r9] \n" \
        "movq %%r10, %[r10]\n" \
        "movq %%r11, %[r11]\n" \
        "movq %%r12, %[r12]\n" \
        "movq %%r13, %[r13]\n" \
        "movq %%r14, %[r14]\n" \
        "movq %%r15, %[r15]\n" \
        : [rax]"=m"((r).rax), [rbx]"=m"((r).rbx), \
          [rcx]"=m"((r).rcx), [rdx]"=m"((r).rdx), \
          [rsi]"=m"((r).rsi), [rdi]"=m"((r).rdi), \
          [rbp]"=m"((r).rbp), \
          [r8] "=m"((r).r8),  [r9] "=m"((r).r9),  \
          [r10]"=m"((r).r10), [r11]"=m"((r).r11), \
          [r12]"=m"((r).r12), [r13]"=m"((r).r13), \
          [r14]"=m"((r).r14), [r15]"=m"((r).r15)  \
        : : "memory")

#endif