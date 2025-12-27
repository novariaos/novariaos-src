#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

extern uint64_t uptime_seconds;
extern uint32_t tick_counter;

uint64_t get_uptime(void);
void pit_init(void);
void pit_handler(void);
void pit_poll(void);

#endif
