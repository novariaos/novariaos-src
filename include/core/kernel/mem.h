#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stddef.h>
#include <stdint.h>

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

#define MAGIC_ALLOC      0xABCD1234
#define MAGIC_FREE       0xDCBA5678
#define ALIGNMENT        8
#define MIN_BLOCK_SIZE   (sizeof(MemoryBlock) + ALIGNMENT)

typedef struct MemoryBlock {
    int32_t magic;
    size_t size;
    struct MemoryBlock* next;
} MemoryBlock;

static MemoryBlock* freeList = NULL;

static void* poolStart = NULL;
static size_t poolSizeTotal = 0;
static uint64_t hhdmOffset = 0;

static void mergeFreeBlocks();
static bool validateBlock(MemoryBlock* block);
static void* physicalToVirtual(uint64_t physical);
static uint64_t virtualToPhysical(void* virtual);

// Aliases for convenience
#define kmalloc allocateMemory
#define kfree freeMemory

#endif
