#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include <stdint.h>

#define IDT_SIZE          256
#define INTERRUPT_GATE    0x8E
#define TRAP_GATE         0x8F
#define KERNEL_CS         0x28

#define ENTER_KEY_CODE    0x1C
#define MAX_TEXT_SIZE     1024
#define SYSCALL_INTERRUPT 0x80

/* IDT entry (16 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t offset_0_15;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_16_31;
    uint32_t offset_32_63;
    uint32_t zero;
} idt_entry_t;

/* IDT register */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

/* Frame pushed by the CPU on interrupt entry */
typedef struct {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

void idt_init(void);
void idt_load(void);
void idt_install_handler(uint8_t vector, void* handler);
void panic_fb_init(uint32_t *addr, uint32_t w, uint32_t h, uint32_t pitch_px);

#endif /* ARCH_IDT_H */
