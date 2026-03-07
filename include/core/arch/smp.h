#ifndef SMP_H
#define SMP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#define MAX_CPUS 64
#define CPU_STACK_SIZE 16384

typedef enum {
    CPU_STATE_OFFLINE = 0,
    CPU_STATE_STARTING,
    CPU_STATE_ONLINE,
    CPU_STATE_HALTED,
} cpu_state_t;

typedef struct cpu_info {
    uint32_t     cpu_id;
    uint32_t     lapic_id;
    cpu_state_t  state;
    void*        stack;
    volatile int ready;
} cpu_info_t;

extern cpu_info_t cpus[MAX_CPUS];
extern volatile uint32_t cpu_count;
extern volatile uint32_t cpus_online;

void     smp_init(struct limine_mp_response* mp);
uint32_t smp_cpu_count(void);
uint32_t smp_current_cpu_id(void);
void     smp_send_ipi(uint32_t lapic_id, uint8_t vector);
void     smp_halt_all(void);

#endif
