// SPDX-License-Identifier: GPL-3.0-only

#ifndef CORE_DRIVERS_TIMER_H
#define CORE_DRIVERS_TIMER_H

#include <stdint.h>
#include <stddef.h>

typedef struct timer_driver {
    uint64_t (*get_time)(void);
    const char* (*get_name)(void);
    
    uint64_t (*get_uptime)(void);
    uint64_t (*get_unix_epoch)(void);
    void (*get_readable)(char* buf, size_t size);
    void (*init)(void);
    void (*enable_interrupt)(uint32_t hz);
    void (*disable_interrupt)(void);
    
    const char* name;
} timer_driver_t;

void init_timer_subsystem(void);

uint64_t timer_get_uptime(void);
uint64_t timer_get_time(void);
uint64_t timer_get_unix_epoch(void);
void timer_get_readable(char* buf, size_t size);
const char* timer_get_name(void);

#endif // CORE_DRIVERS_TIMER_H