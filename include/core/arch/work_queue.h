#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <core/arch/smp.h>
#include <core/arch/spinlock.h>

#define WQ_MAX_ITEMS    256
#define WQ_IPI_VECTOR   0xF0

typedef void (*work_fn_t)(void* arg);

typedef struct {
    work_fn_t fn;
    void*     arg;
} work_item_t;

typedef struct {
    work_item_t   items[WQ_MAX_ITEMS];
    volatile uint32_t head;
    volatile uint32_t tail;
    spinlock_t    lock;
} cpu_work_queue_t;

extern cpu_work_queue_t cpu_wqs[MAX_CPUS];

void wq_init(void);
int  wq_submit(uint32_t cpu_id, work_fn_t fn, void* arg);
void wq_run(uint32_t cpu_id);
int  wq_submit_any(work_fn_t fn, void* arg);
uint32_t wq_pending(uint32_t cpu_id);

#endif
