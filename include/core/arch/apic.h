// SPDX-License-Identifier: GPL-3.0-only

#ifndef ARCH_APIC_H
#define ARCH_APIC_H

#include <stdint.h>
#include <stdbool.h>

// Default LAPIC physical base address
#define LAPIC_BASE               0xFEE00000ULL

// LAPIC register offsets
#define LAPIC_ID                 0x020
#define LAPIC_VERSION            0x030
#define LAPIC_EOI                0x0B0
#define LAPIC_SVR                0x0F0
#define LAPIC_LVT_TIMER          0x320
#define LAPIC_LVT_LINT0          0x350
#define LAPIC_LVT_LINT1          0x360
#define LAPIC_LVT_ERROR          0x370
#define LAPIC_TIMER_INITCNT      0x380
#define LAPIC_TIMER_CURRCNT      0x390
#define LAPIC_TIMER_DIV          0x3E0

// SVR bits
#define LAPIC_SVR_ENABLE         (1 << 8)
#define LAPIC_SVR_SPURIOUS_VEC   0xFF

// LVT timer modes
#define LAPIC_TIMER_ONESHOT      (0 << 17)
#define LAPIC_TIMER_PERIODIC     (1 << 17)

// LVT mask bit
#define LAPIC_LVT_MASKED         (1 << 16)

// APIC timer divide values (LAPIC_TIMER_DIV register)
#define LAPIC_TIMER_DIV_16       0x3

// IDT vectors
#define APIC_TIMER_VECTOR        0x20
#define APIC_SPURIOUS_VECTOR     0xFF

bool apic_init(void);
bool apic_available(void);
void apic_eoi(void);
uint64_t apic_get_uptime_ms(void);

#endif // ARCH_APIC_H
