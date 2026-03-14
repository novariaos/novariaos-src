#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include <core/kernel/mem/buddy.h>

void     memory_manager_init(void);
uint64_t get_hhdm_offset(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
buddy_allocator_t* slab_get_buddy(void);
size_t get_memory_total(void);
size_t get_memory_free(void);
size_t get_memory_used(void);
size_t get_memory_available(void);
void format_memory_size(size_t size, char* buffer);
void memory_test(void);
void check_memory_leaks(void);
uint64_t get_hhdm_offset(void);

#endif
