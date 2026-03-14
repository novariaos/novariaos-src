// SPDX-License-Identifier: GPL-3.0-only

#include <core/arch/idt.h>
#include <core/arch/io.h>
#include <stdint.h>

#define COM1 0x3F8

static void serial_write(char c) {
    outb(COM1, (uint8_t)c);
}

static void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n') serial_write('\r');
        serial_write(*s++);
    }
}

static void serial_hex64(uint64_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        uint8_t nib = (v >> (i * 4)) & 0xF;
        buf[17 - i] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[18] = '\0';
    serial_puts(buf);
}

static idt_entry_t idt_table[IDT_SIZE];
static idtr_t      idt_idtr;

// Panic framebuffer state — set once by panic_fb_init(), used directly
// by exception_halt() without touching any HAL layer.
static volatile uint32_t *pfb_addr     = 0;
static volatile uint32_t  pfb_width    = 0;
static volatile uint32_t  pfb_height   = 0;
static volatile uint32_t  pfb_pitch_px = 0;

void panic_fb_init(uint32_t *addr, uint32_t w, uint32_t h, uint32_t pitch_px) {
    pfb_addr     = addr;
    pfb_width    = w;
    pfb_height   = h;
    pfb_pitch_px = pitch_px;
}

extern const uint8_t builtin_font[128][8];

#define PANIC_BG  0x0D1B2A
#define PANIC_FG  0xCDD6F4
#define PANIC_HL  0x89DCEB
#define PANIC_DIM 0x6C7086

static uint32_t pfb_cur_x = 0;
static uint32_t pfb_cur_y = 0;

static void pfb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!pfb_addr || x >= pfb_width || y >= pfb_height) return;
    pfb_addr[y * pfb_pitch_px + x] = color;
}

static void pfb_clear(void) {
    if (!pfb_addr) return;
    for (uint32_t y = 0; y < pfb_height; y++)
        for (uint32_t x = 0; x < pfb_width; x++)
            pfb_addr[y * pfb_pitch_px + x] = PANIC_BG;
    pfb_cur_x = 0;
    pfb_cur_y = 0;
}

static void pfb_putchar(char c, uint32_t fg) {
    if (!pfb_addr) return;
    if (c == '\n') {
        pfb_cur_x  = 0;
        pfb_cur_y += 10;
        return;
    }
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) idx = '?';
    for (int row = 0; row < 8; row++) {
        uint8_t bits = builtin_font[idx][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : PANIC_BG;
            pfb_putpixel(pfb_cur_x + (uint32_t)col,
                         pfb_cur_y + (uint32_t)row, color);
        }
    }
    pfb_cur_x += 9;
    if (pfb_cur_x + 9 > pfb_width) {
        pfb_cur_x  = 0;
        pfb_cur_y += 10;
    }
}

static void pfb_puts(const char* s, uint32_t fg) {
    while (*s) pfb_putchar(*s++, fg);
}

static void pfb_hex64(uint64_t v, uint32_t fg) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        uint8_t nib = (v >> (i * 4)) & 0xF;
        buf[17 - i] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[18] = '\0';
    pfb_puts(buf, fg);
}

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
} cpu_regs_t;

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

static void __attribute__((noreturn))
exception_halt(const char* name, interrupt_frame_t* frame,
               uint64_t error_code, int has_err,
               const cpu_regs_t* regs)
{
    __asm__ volatile("cli");

    uint64_t cr2, cr3;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    // Serial output
    serial_puts("\n\n:( *** KERNEL PANIC ***\n");
    serial_puts(name); serial_puts("\n\n");
    serial_puts("RIP    = "); serial_hex64(frame->rip);    serial_puts("\n");
    serial_puts("CS     = "); serial_hex64(frame->cs);     serial_puts("\n");
    serial_puts("RFLAGS = "); serial_hex64(frame->rflags); serial_puts("\n");
    serial_puts("RSP    = "); serial_hex64(frame->rsp);    serial_puts("\n");
    serial_puts("SS     = "); serial_hex64(frame->ss);     serial_puts("\n");
    serial_puts("CR2    = "); serial_hex64(cr2);           serial_puts("\n");
    serial_puts("CR3    = "); serial_hex64(cr3);           serial_puts("\n");
    if (has_err) {
        serial_puts("ERR    = "); serial_hex64(error_code); serial_puts("\n");
    }
    if (regs) {
        serial_puts("RAX    = "); serial_hex64(regs->rax); serial_puts("\n");
        serial_puts("RBX    = "); serial_hex64(regs->rbx); serial_puts("\n");
        serial_puts("RCX    = "); serial_hex64(regs->rcx); serial_puts("\n");
        serial_puts("RDX    = "); serial_hex64(regs->rdx); serial_puts("\n");
        serial_puts("RSI    = "); serial_hex64(regs->rsi); serial_puts("\n");
        serial_puts("RDI    = "); serial_hex64(regs->rdi); serial_puts("\n");
        serial_puts("RBP    = "); serial_hex64(regs->rbp); serial_puts("\n");
        serial_puts("R8     = "); serial_hex64(regs->r8);  serial_puts("\n");
        serial_puts("R9     = "); serial_hex64(regs->r9);  serial_puts("\n");
        serial_puts("R10    = "); serial_hex64(regs->r10); serial_puts("\n");
        serial_puts("R11    = "); serial_hex64(regs->r11); serial_puts("\n");
        serial_puts("R12    = "); serial_hex64(regs->r12); serial_puts("\n");
        serial_puts("R13    = "); serial_hex64(regs->r13); serial_puts("\n");
        serial_puts("R14    = "); serial_hex64(regs->r14); serial_puts("\n");
        serial_puts("R15    = "); serial_hex64(regs->r15); serial_puts("\n");
    }
    serial_puts("\nPlease report this issue at github.com/novariaos/novariaos-src\n");
    serial_puts("System halted. -- NovariaOS\n");

    // Screen output (direct framebuffer, no HAL)
    pfb_clear();

    pfb_cur_x = 8; pfb_cur_y = 16;
    pfb_puts(":( ", PANIC_FG);
    pfb_puts("*** KERNEL PANIC ***", PANIC_HL);
    pfb_cur_y += 14;
    pfb_cur_x = 8;
    pfb_puts(name, PANIC_FG);

    pfb_cur_y += 12; pfb_cur_x = 8;
    pfb_puts("-- CPU State --", PANIC_DIM);

    pfb_cur_y += 12; pfb_cur_x = 8;
    pfb_puts("RIP    = ", PANIC_DIM); pfb_hex64(frame->rip,    PANIC_FG);
    pfb_puts("   CS     = ", PANIC_DIM); pfb_hex64(frame->cs,  PANIC_FG);

    pfb_cur_y += 10; pfb_cur_x = 8;
    pfb_puts("RFLAGS = ", PANIC_DIM); pfb_hex64(frame->rflags, PANIC_FG);
    pfb_puts("   RSP    = ", PANIC_DIM); pfb_hex64(frame->rsp, PANIC_FG);

    pfb_cur_y += 10; pfb_cur_x = 8;
    pfb_puts("SS     = ", PANIC_DIM); pfb_hex64(frame->ss,     PANIC_FG);
    pfb_puts("   CR2    = ", PANIC_DIM); pfb_hex64(cr2,        PANIC_FG);

    pfb_cur_y += 10; pfb_cur_x = 8;
    pfb_puts("CR3    = ", PANIC_DIM); pfb_hex64(cr3,           PANIC_FG);
    if (has_err) {
        pfb_puts("   ERR    = ", PANIC_DIM); pfb_hex64(error_code, PANIC_FG);
    }

    if (regs) {
        pfb_cur_y += 12; pfb_cur_x = 8;
        pfb_puts("-- General-Purpose Registers --", PANIC_DIM);

        pfb_cur_y += 12; pfb_cur_x = 8;
        pfb_puts("RAX = ", PANIC_DIM); pfb_hex64(regs->rax, PANIC_FG);
        pfb_puts("  RBX = ", PANIC_DIM); pfb_hex64(regs->rbx, PANIC_FG);
        pfb_puts("  RCX = ", PANIC_DIM); pfb_hex64(regs->rcx, PANIC_FG);

        pfb_cur_y += 10; pfb_cur_x = 8;
        pfb_puts("RDX = ", PANIC_DIM); pfb_hex64(regs->rdx, PANIC_FG);
        pfb_puts("  RSI = ", PANIC_DIM); pfb_hex64(regs->rsi, PANIC_FG);
        pfb_puts("  RDI = ", PANIC_DIM); pfb_hex64(regs->rdi, PANIC_FG);

        pfb_cur_y += 10; pfb_cur_x = 8;
        pfb_puts("RBP = ", PANIC_DIM); pfb_hex64(regs->rbp, PANIC_FG);
        pfb_puts("  R8  = ", PANIC_DIM); pfb_hex64(regs->r8,  PANIC_FG);
        pfb_puts("  R9  = ", PANIC_DIM); pfb_hex64(regs->r9,  PANIC_FG);

        pfb_cur_y += 10; pfb_cur_x = 8;
        pfb_puts("R10 = ", PANIC_DIM); pfb_hex64(regs->r10, PANIC_FG);
        pfb_puts("  R11 = ", PANIC_DIM); pfb_hex64(regs->r11, PANIC_FG);
        pfb_puts("  R12 = ", PANIC_DIM); pfb_hex64(regs->r12, PANIC_FG);

        pfb_cur_y += 10; pfb_cur_x = 8;
        pfb_puts("R13 = ", PANIC_DIM); pfb_hex64(regs->r13, PANIC_FG);
        pfb_puts("  R14 = ", PANIC_DIM); pfb_hex64(regs->r14, PANIC_FG);
        pfb_puts("  R15 = ", PANIC_DIM); pfb_hex64(regs->r15, PANIC_FG);
    }

    pfb_cur_y += 16; pfb_cur_x = 8;
    pfb_puts("Please report this issue at github.com/novariaos/novariaos-src", PANIC_DIM);
    pfb_cur_y += 12; pfb_cur_x = 8;
    pfb_puts("NovariaOS  |  System halted", PANIC_HL);

    for (;;) __asm__ volatile("hlt");
}

// EXC_NOERR: no error code pushed by CPU
#define EXC_NOERR(fn, msg)                                                     \
    static void __attribute__((interrupt, target("general-regs-only")))        \
    fn(interrupt_frame_t* frame) {                                             \
        cpu_regs_t _r;                                                         \
        CAPTURE_REGS(_r);                                                      \
        exception_halt(msg, frame, 0, 0, &_r);                                \
    }

// EXC_ERR: CPU pushes an error code as second argument
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
