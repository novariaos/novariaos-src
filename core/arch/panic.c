#include <core/arch/panic.h>
#include <core/drivers/serial.h>
#include <core/kernel/vge/fb_render.h>
#include <stdint.h>

#define PANIC_BG  0x0D1B2A
#define PANIC_FG  0xCDD6F4
#define PANIC_HL  0x89DCEB
#define PANIC_DIM 0x6C7086

extern const uint8_t builtin_font[128][8];

static volatile uint32_t *pfb_addr     = 0;
static volatile uint32_t  pfb_width    = 0;
static volatile uint32_t  pfb_height   = 0;
static volatile uint32_t  pfb_pitch_px = 0;
static uint32_t pfb_cur_x = 0;
static uint32_t pfb_cur_y = 0;

void panic_fb_init(uint32_t *addr, uint32_t w, uint32_t h, uint32_t pitch_px) {
    pfb_addr     = addr;
    pfb_width    = w;
    pfb_height   = h;
    pfb_pitch_px = pitch_px;
}

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

static void serial_hex64(uint64_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        uint8_t nib = (v >> (i * 4)) & 0xF;
        buf[17 - i] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[18] = '\0';
    serial_print(buf);
}

void exception_halt(const char* name, interrupt_frame_t* frame,
                    uint64_t error_code, int has_err,
                    const cpu_regs_t* regs)
{
    __asm__ volatile("cli");

    uint64_t cr2, cr3;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    serial_print("\n\n:( *** KERNEL PANIC ***\n");
    serial_print(name); serial_print("\n\n");
    serial_print("RIP    = "); serial_hex64(frame->rip);    serial_print("\n");
    serial_print("CS     = "); serial_hex64(frame->cs);     serial_print("\n");
    serial_print("RFLAGS = "); serial_hex64(frame->rflags); serial_print("\n");
    serial_print("RSP    = "); serial_hex64(frame->rsp);    serial_print("\n");
    serial_print("SS     = "); serial_hex64(frame->ss);     serial_print("\n");
    serial_print("CR2    = "); serial_hex64(cr2);           serial_print("\n");
    serial_print("CR3    = "); serial_hex64(cr3);           serial_print("\n");
    if (has_err) {
        serial_print("ERR    = "); serial_hex64(error_code); serial_print("\n");
    }
    if (regs) {
        serial_print("RAX    = "); serial_hex64(regs->rax); serial_print("\n");
        serial_print("RBX    = "); serial_hex64(regs->rbx); serial_print("\n");
        serial_print("RCX    = "); serial_hex64(regs->rcx); serial_print("\n");
        serial_print("RDX    = "); serial_hex64(regs->rdx); serial_print("\n");
        serial_print("RSI    = "); serial_hex64(regs->rsi); serial_print("\n");
        serial_print("RDI    = "); serial_hex64(regs->rdi); serial_print("\n");
        serial_print("RBP    = "); serial_hex64(regs->rbp); serial_print("\n");
        serial_print("R8     = "); serial_hex64(regs->r8);  serial_print("\n");
        serial_print("R9     = "); serial_hex64(regs->r9);  serial_print("\n");
        serial_print("R10    = "); serial_hex64(regs->r10); serial_print("\n");
        serial_print("R11    = "); serial_hex64(regs->r11); serial_print("\n");
        serial_print("R12    = "); serial_hex64(regs->r12); serial_print("\n");
        serial_print("R13    = "); serial_hex64(regs->r13); serial_print("\n");
        serial_print("R14    = "); serial_hex64(regs->r14); serial_print("\n");
        serial_print("R15    = "); serial_hex64(regs->r15); serial_print("\n");
    }
    serial_print("\nPlease report this issue at https://github.com/novariaos/novariaos-src\n");
    serial_print("System halted. -- NovariaOS\n");

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
    pfb_puts("Please report this issue at https://github.com/novariaos/novariaos-src", PANIC_DIM);
    pfb_cur_y += 12; pfb_cur_x = 8;
    pfb_puts("NovariaOS  |  System halted", PANIC_HL);

    for (;;) __asm__ volatile("hlt");
}

void panic(const char* message) {
    asm volatile ("cli");
    serial_print("\nKERNEL PANIC: ");
    serial_print(message);
    serial_print("\n");
    
    if (pfb_addr) {
        pfb_clear();
        pfb_cur_x = 8; pfb_cur_y = 16;
        pfb_puts(":( ", PANIC_FG);
        pfb_puts("KERNEL PANIC", PANIC_HL);
        pfb_cur_y += 14;
        pfb_cur_x = 8;
        pfb_puts(message, PANIC_FG);
    }

    for (;;) {
        asm volatile("hlt");
    }
}