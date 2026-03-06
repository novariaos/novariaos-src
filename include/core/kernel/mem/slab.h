#ifndef SLAB_H
#define SLAB_H

#include <stddef.h>
#include <stdint.h>

#define SLAB_SIZES_COUNT  8
#define SLAB_OBJ_SIZES    {8, 16, 32, 64, 128, 256, 512, 1024}
#define SLAB_PAGE_SIZE    4096
#define SLAB_MAGIC        0x5AB50BEC
#define SLAB_MAX_CPUS     64

void  slab_init(void);
void* slab_alloc(size_t size);
void  slab_free(void* ptr);
int   slab_owns(void* ptr);

void  slab_cpu_init(uint32_t cpu_id);
void* slab_alloc_cpu(uint32_t cpu_id, size_t size);
void  slab_free_cpu(void* ptr);

#endif
