#ifndef MEM_H
#define MEM_H

#include <stddef.h>

#include <core/kernel/kstd.h>
#include <core/drivers/serial.h>

void initializeMemoryManager(void);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
void* allocateMemory(size_t size);
void freeMemory(void* ptr);
void mm_test();
size_t getMemTotal(void);
size_t getMemFree(void);
size_t getMemAvailable(void);
void formatMemorySize(size_t size, char* buffer);

// Aliases for convenience
#define kmalloc allocateMemory
#define kfree freeMemory

#endif
