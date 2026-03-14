// SPDX-License-Identifier: GPL-3.0-only

#include <core/arch/idt.h>

static idt_entry_t idt_table[IDT_SIZE];
static idtr_t      idt_idtr;

static void __attribute__((interrupt, target("general-regs-only")))
idt_default_handler(interrupt_frame_t* frame) {
    (void)frame;
    /* Send EOI to LAPIC so future interrupts are not masked. */
    volatile uint32_t* lapic_eoi = (volatile uint32_t*)0xFEE000B0ULL;
    *lapic_eoi = 0;
}

static void idt_set_entry(uint8_t vector, void* handler) {
    uint64_t addr = (uint64_t)handler;
    idt_table[vector].offset_0_15  = (uint16_t)(addr & 0xFFFF);
    idt_table[vector].selector     = KERNEL_CS;
    idt_table[vector].ist          = 0;
    idt_table[vector].type_attr    = INTERRUPT_GATE;
    idt_table[vector].offset_16_31 = (uint16_t)((addr >> 16) & 0xFFFF);
    idt_table[vector].offset_32_63 = (uint32_t)(addr >> 32);
    idt_table[vector].zero         = 0;
}

void idt_init(void) {
    for (int i = 0; i < IDT_SIZE; i++)
        idt_set_entry((uint8_t)i, idt_default_handler);

    idt_idtr.limit = sizeof(idt_table) - 1;
    idt_idtr.base  = (uint64_t)idt_table;
    __asm__ volatile("lidt %0" : : "m"(idt_idtr));
}

void idt_install_handler(uint8_t vector, void* handler) {
    idt_set_entry(vector, handler);
}
