#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

void memory_manager_init(void);
void* kmalloc(size_t size);
void* krealloc(void* ptr, size_t new_size);
void kfree(void* ptr);
size_t get_memory_total(void);
size_t get_memory_free(void);
size_t get_memory_used(void);
size_t get_memory_available(void);
void format_memory_size(size_t size, char* buffer);
void memory_test(void);
void check_memory_leaks(void);

#endif
