// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/mem/cpu_pool.h>
#include <core/kernel/mem/buddy.h>
#include <core/kernel/kstd.h>
#include <log.h>

extern buddy_allocator_t* slab_get_buddy(void);

static const uint32_t pool_orders[CPU_POOL_ORDERS] = {12, 13, 14};

typedef struct {
    void*    blocks[CPU_POOL_CACHE_SIZE];
    uint32_t count;
} cpu_order_pool_t;

typedef struct {
    cpu_order_pool_t orders[CPU_POOL_ORDERS];
    uint8_t          initialized;
} cpu_pool_t;

static cpu_pool_t cpu_pools[CPU_POOL_MAX_CPUS];

static int order_idx(uint32_t order) {
    for (int i = 0; i < CPU_POOL_ORDERS; i++)
        if (pool_orders[i] == order) return i;
    return -1;
}

void cpu_pool_init(uint32_t cpu_id) {
    if (cpu_id >= CPU_POOL_MAX_CPUS) return;
    cpu_pool_t* p = &cpu_pools[cpu_id];
    for (int i = 0; i < CPU_POOL_ORDERS; i++)
        p->orders[i].count = 0;
    p->initialized = 1;
}

void* cpu_pool_alloc(uint32_t cpu_id, uint32_t order) {
    buddy_allocator_t* buddy = slab_get_buddy();
    size_t bsize = (size_t)1 << order;

    if (cpu_id >= CPU_POOL_MAX_CPUS) return buddy_alloc(buddy, bsize);
    cpu_pool_t* p = &cpu_pools[cpu_id];
    if (!p->initialized) return buddy_alloc(buddy, bsize);

    int idx = order_idx(order);
    if (idx < 0) return buddy_alloc(buddy, bsize);

    cpu_order_pool_t* op = &p->orders[idx];
    if (op->count > 0)
        return op->blocks[--op->count];

    uint32_t refill = CPU_POOL_CACHE_SIZE / 2;
    for (uint32_t i = 0; i < refill && op->count < CPU_POOL_CACHE_SIZE; i++) {
        void* b = buddy_alloc(buddy, bsize);
        if (!b) break;
        op->blocks[op->count++] = b;
    }
    if (op->count > 0)
        return op->blocks[--op->count];

    return buddy_alloc(buddy, bsize);
}

void cpu_pool_free(uint32_t cpu_id, void* ptr, uint32_t order) {
    if (!ptr) return;
    buddy_allocator_t* buddy = slab_get_buddy();

    if (cpu_id >= CPU_POOL_MAX_CPUS) { buddy_free(buddy, ptr, order); return; }
    cpu_pool_t* p = &cpu_pools[cpu_id];
    if (!p->initialized) { buddy_free(buddy, ptr, order); return; }

    int idx = order_idx(order);
    if (idx < 0) { buddy_free(buddy, ptr, order); return; }

    cpu_order_pool_t* op = &p->orders[idx];
    if (op->count < CPU_POOL_CACHE_SIZE) {
        op->blocks[op->count++] = ptr;
        return;
    }

    uint32_t flush = CPU_POOL_CACHE_SIZE / 2;
    if (op->count < flush) flush = op->count;
    for (uint32_t i = 0; i < flush; i++)
        buddy_free(buddy, op->blocks[--op->count], order);
    op->blocks[op->count++] = ptr;
}
