// SPDX-License-Identifier: GPL-3.0-only

#include <core/arch/apic.h>
#include <core/arch/idt.h>
#include <core/arch/io.h>
#include <core/kernel/kstd.h>
#include <core/kernel/tty.h>
#include <stdint.h>
#include <stdbool.h>
#include <log.h>

// PIT constants for calibration
#define PIT_FREQUENCY            1193182
#define PIT_CMD                  0x43
#define PIT_CHANNEL0             0x40
#define CALIBRATION_MS           10

static bool lapic_enabled = false;
static uint32_t lapic_ticks_per_ms = 0;
static volatile uint64_t apic_uptime_ms = 0;

static inline uint32_t lapic_read(uint32_t offset) {
    return *(volatile uint32_t*)(LAPIC_BASE + offset);
}

static inline void lapic_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(LAPIC_BASE + offset) = value;
    __asm__ volatile("" ::: "memory");
}

// Mask all legacy 8259 PIC IRQs
static void pic_disable(void) {
    // Remap PIC to avoid conflicts with CPU exception vectors
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    // Mask all IRQs on both PIC chips
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

// Calibrate LAPIC timer against PIT channel 0 (~10ms window)
static uint32_t lapic_calibrate(void) {
    const uint16_t pit_ticks = (uint16_t)((uint32_t)PIT_FREQUENCY * CALIBRATION_MS / 1000);

    // Program PIT channel 0: mode 0 (one-shot), lo/hi byte, binary
    outb(PIT_CMD, 0x30);
    outb(PIT_CHANNEL0, (uint8_t)(pit_ticks & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(pit_ticks >> 8));

    // Start LAPIC timer in one-shot mode with max count
    lapic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED | LAPIC_TIMER_ONESHOT | APIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITCNT, 0xFFFFFFFF);

    // Wait for PIT countdown (detect wrap-around through 0)
    uint16_t last = 0xFFFF;
    while (true) {
        outb(PIT_CMD, 0x00); // latch channel 0
        uint8_t lo = inb(PIT_CHANNEL0);
        uint8_t hi = inb(PIT_CHANNEL0);
        uint16_t curr = lo | ((uint16_t)hi << 8);
        if (curr == 0 || curr > last) break;
        last = curr;
    }

    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURRCNT);
    lapic_write(LAPIC_TIMER_INITCNT, 0);

    return elapsed / CALIBRATION_MS;
}

// LAPIC timer ISR
static void __attribute__((interrupt, target("general-regs-only")))
apic_timer_handler(interrupt_frame_t* frame) {
    (void)frame;
    apic_uptime_ms++;
    lapic_write(LAPIC_EOI, 0);
}

bool apic_init(void) {
    LOG_INFO("APIC: Disabling legacy PIC\n");
    pic_disable();

    LOG_INFO("APIC: Enabling LAPIC\n");

    // Enable LAPIC via Spurious Interrupt Vector Register
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | LAPIC_SVR_SPURIOUS_VEC);

    // Mask LINT0/LINT1 to avoid spurious NMIs
    lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LAPIC_LVT_MASKED);

    LOG_INFO("APIC: Calibrating timer\n");
    lapic_ticks_per_ms = lapic_calibrate();

    if (lapic_ticks_per_ms == 0) {
        LOG_ERROR("APIC: Calibration failed\n");
        return false;
    }

    LOG_INFO("APIC: Timer frequency: %u ticks/ms\n", lapic_ticks_per_ms);

    // Install ISR and start periodic timer (1ms period)
    idt_install_handler(APIC_TIMER_VECTOR, apic_timer_handler);
    lapic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_PERIODIC | APIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITCNT, lapic_ticks_per_ms);

    lapic_enabled = true;
    LOG_INFO("APIC: Initialized successfully\n");
    return true;
}

bool apic_available(void) {
    return lapic_enabled;
}

void apic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

uint64_t apic_get_uptime_ms(void) {
    return apic_uptime_ms;
}
