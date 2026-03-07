#include <core/arch/work_queue.h>
#include <core/arch/smp.h>
#include <core/arch/spinlock.h>
#include <log.h>

cpu_work_queue_t cpu_wqs[MAX_CPUS];

void wq_init(void) {
    for (uint32_t i = 0; i < MAX_CPUS; i++) {
        cpu_wqs[i].head = 0;
        cpu_wqs[i].tail = 0;
        spinlock_init(&cpu_wqs[i].lock);
    }
}

int wq_submit(uint32_t cpu_id, work_fn_t fn, void* arg) {
    if (cpu_id >= cpu_count) return -1;
    if (!fn) return -1;

    cpu_work_queue_t* wq = &cpu_wqs[cpu_id];
    spinlock_acquire(&wq->lock);

    uint32_t next = (wq->tail + 1) % WQ_MAX_ITEMS;
    if (next == wq->head) {
        spinlock_release(&wq->lock);
        LOG_WARN("wq: cpu%u queue full\n", cpu_id);
        return -1;
    }

    wq->items[wq->tail].fn  = fn;
    wq->items[wq->tail].arg = arg;
    wq->tail = next;

    spinlock_release(&wq->lock);

    return 0;
}

void wq_run(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return;
    cpu_work_queue_t* wq = &cpu_wqs[cpu_id];

    while (1) {
        spinlock_acquire(&wq->lock);
        if (wq->head == wq->tail) {
            spinlock_release(&wq->lock);
            break;
        }
        work_item_t item = wq->items[wq->head];
        wq->head = (wq->head + 1) % WQ_MAX_ITEMS;
        spinlock_release(&wq->lock);

        item.fn(item.arg);
    }
}

int wq_submit_any(work_fn_t fn, void* arg) {
    uint32_t best_cpu  = UINT32_MAX;
    uint32_t min_items = WQ_MAX_ITEMS + 1;

    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].state != CPU_STATE_ONLINE) continue;
        cpu_work_queue_t* wq = &cpu_wqs[i];
        uint32_t pending = (wq->tail - wq->head + WQ_MAX_ITEMS) % WQ_MAX_ITEMS;
        if (pending < min_items) {
            min_items = pending;
            best_cpu  = i;
        }
    }
    if (best_cpu == UINT32_MAX) return -1;
    return wq_submit(best_cpu, fn, arg);
}

uint32_t wq_pending(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return 0;
    cpu_work_queue_t* wq = &cpu_wqs[cpu_id];
    spinlock_acquire(&wq->lock);
    uint32_t n = (wq->tail - wq->head + WQ_MAX_ITEMS) % WQ_MAX_ITEMS;
    spinlock_release(&wq->lock);
    return n;
}
