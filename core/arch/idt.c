#include <core/arch/idt.h>
#include <core/arch/panic.h>
#include <stdint.h>

static idt_entry_t idt_table[IDT_SIZE];
static idtr_t      idt_idtr;

#define EXC_NOERR(fn, msg)                                                     \
    static void __attribute__((interrupt, target("general-regs-only")))        \
    fn(interrupt_frame_t* frame) {                                             \
        cpu_regs_t _r;                                                         \
        CAPTURE_REGS(_r);                                                      \
        exception_halt(msg, frame, 0, 0, &_r);                                \
    }

#define EXC_ERR(fn, msg)                                                       \
    static void __attribute__((interrupt, target("general-regs-only")))        \
    fn(interrupt_frame_t* frame, uint64_t err) {                               \
        cpu_regs_t _r;                                                         \
        CAPTURE_REGS(_r);                                                      \
        exception_halt(msg, frame, err, 1, &_r);                              \
    }

EXC_NOERR(exc_de,  "#DE: Divide Error")
EXC_NOERR(exc_db,  "#DB: Debug Exception")
EXC_NOERR(exc_nmi, "NMI: Non-Maskable Interrupt")
EXC_NOERR(exc_bp,  "#BP: Breakpoint")
EXC_NOERR(exc_of,  "#OF: Overflow")
EXC_NOERR(exc_br,  "#BR: Bound Range Exceeded")
EXC_NOERR(exc_ud,  "#UD: Invalid Opcode")
EXC_NOERR(exc_nm,  "#NM: Device Not Available")
EXC_ERR  (exc_df,  "#DF: Double Fault")
EXC_ERR  (exc_ts,  "#TS: Invalid TSS")
EXC_ERR  (exc_np,  "#NP: Segment Not Present")
EXC_ERR  (exc_ss,  "#SS: Stack-Segment Fault")
EXC_ERR  (exc_gp,  "#GP: General Protection Fault")
EXC_ERR  (exc_pf,  "#PF: Page Fault")
EXC_NOERR(exc_mf,  "#MF: x87 Floating-Point Exception")
EXC_ERR  (exc_ac,  "#AC: Alignment Check")
EXC_NOERR(exc_mc,  "#MC: Machine Check")
EXC_NOERR(exc_xf,  "#XF: SIMD Floating-Point Exception")

static void __attribute__((interrupt, target("general-regs-only")))
idt_default_handler(interrupt_frame_t* frame) {
    (void)frame;
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

    idt_set_entry(0x00, exc_de);
    idt_set_entry(0x01, exc_db);
    idt_set_entry(0x02, exc_nmi);
    idt_set_entry(0x03, exc_bp);
    idt_set_entry(0x04, exc_of);
    idt_set_entry(0x05, exc_br);
    idt_set_entry(0x06, exc_ud);
    idt_set_entry(0x07, exc_nm);
    idt_set_entry(0x08, exc_df);
    idt_set_entry(0x0A, exc_ts);
    idt_set_entry(0x0B, exc_np);
    idt_set_entry(0x0C, exc_ss);
    idt_set_entry(0x0D, exc_gp);
    idt_set_entry(0x0E, exc_pf);
    idt_set_entry(0x10, exc_mf);
    idt_set_entry(0x11, exc_ac);
    idt_set_entry(0x12, exc_mc);
    idt_set_entry(0x13, exc_xf);

    idt_idtr.limit = sizeof(idt_table) - 1;
    idt_idtr.base  = (uint64_t)idt_table;
    __asm__ volatile("lidt %0" : : "m"(idt_idtr));
}

void idt_load(void) {
    __asm__ volatile("lidt %0" : : "m"(idt_idtr));
}

void idt_install_handler(uint8_t vector, void* handler) {
    idt_set_entry(vector, handler);
}