#ifndef CPU_POOL_H
#define CPU_POOL_H

#include <stdint.h>
#include <stddef.h>

#define CPU_POOL_MAX_CPUS   64
#define CPU_POOL_ORDERS     3
#define CPU_POOL_CACHE_SIZE 16

void  cpu_pool_init(uint32_t cpu_id);
void* cpu_pool_alloc(uint32_t cpu_id, uint32_t order);
void  cpu_pool_free(uint32_t cpu_id, void* ptr, uint32_t order);

#endif
