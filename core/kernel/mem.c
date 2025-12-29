// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/mem.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>
#include <core/kernel/vge/fb_render.h>
#include <lib/bootloader/limine.h>
#include <core/arch/panic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>

// Limine requests
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};


void formatMemorySize(size_t size, char* buffer) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit_index = 0;
    double formatted_size = (double)size;

    while (formatted_size >= 1024 && unit_index < 3) {
        formatted_size /= 1024;
        unit_index++;
    }

    int int_part = (int)formatted_size;
    int frac_part = (int)((formatted_size - int_part) * 10);
    
    char num_buf[32];
    char* p = num_buf;
    int n = int_part;
    
    if (n == 0) {
        *p++ = '0';
    } else {
        while (n > 0) {
            *p++ = '0' + n % 10;
            n /= 10;
        }
    }
    p--;

    char* buf_ptr = buffer;
    while (p >= num_buf) {
        *buf_ptr++ = *p--;
    }
    *buf_ptr++ = '.';
    *buf_ptr++ = '0' + frac_part;
    *buf_ptr++ = ' ';
    
    const char* unit = units[unit_index];
    while (*unit) {
        *buf_ptr++ = *unit++;
    }
    *buf_ptr = '\0';
}

void initializeMemoryManager(void) {
    // Get HHDM offset
    if (hhdm_request.response == NULL) {
        panic("HHDM request failed");
        return;
    }
    hhdmOffset = hhdm_request.response->offset;

    // Get memory map
    if (memmap_request.response == NULL) {
        panic("Memory map request failed");
        return;
    }

    struct limine_memmap_response* memmap = memmap_request.response;

    // Find the largest usable memory region
    size_t maxUsableSize = 0;
    struct limine_memmap_entry* bestEntry = NULL;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            if (entry->length > maxUsableSize) {
                maxUsableSize = entry->length;
                bestEntry = entry;
            }
        }
    }

    if (bestEntry == NULL || bestEntry->length < MIN_BLOCK_SIZE) {
        panic("No suitable memory region found");
        return;
    }

    // Use the largest usable region for our heap
    poolStart = (void*)(bestEntry->base + hhdmOffset);
    poolSizeTotal = bestEntry->length;

    // Initialize free list
    freeList = (MemoryBlock*)poolStart;
    freeList->magic = MAGIC_FREE;
    freeList->size = poolSizeTotal - sizeof(MemoryBlock);
    freeList->next = NULL;

    char buffer[64];
    formatMemorySize(freeList->size, buffer);
    LOG_INFO("Memory initialized (%s)\n", buffer);
}

void* allocateMemory(size_t size) {
    if (size == 0 || size > poolSizeTotal - sizeof(MemoryBlock)) {
        return NULL;
    }
    
    size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    
    MemoryBlock* prev = NULL;
    MemoryBlock* curr = freeList;

    while (curr != NULL) {
        if (!validateBlock(curr)) {
            panic("Corrupted block in free list");
            return NULL;
        }

        if (curr->size >= size) {
            if (curr->size >= size + MIN_BLOCK_SIZE) {
                MemoryBlock* newBlock = (MemoryBlock*)((char*)curr + sizeof(MemoryBlock) + size);
                
                newBlock->magic = MAGIC_FREE;
                newBlock->size = curr->size - size - sizeof(MemoryBlock);
                newBlock->next = curr->next;
                
                curr->size = size;
                curr->next = newBlock;
            }

            if (prev) prev->next = curr->next;
            else freeList = curr->next;

            curr->magic = MAGIC_ALLOC;
            return (void*)((char*)curr + sizeof(MemoryBlock));
        }
        
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

void freeMemory(void* ptr) {
    if (ptr == NULL) return;

    MemoryBlock* block = (MemoryBlock*)((unsigned long)ptr - sizeof(MemoryBlock));

    // Check if block is within our pool boundaries
    if ((unsigned long)block < (unsigned long)poolStart ||
        (unsigned long)block + block->size + sizeof(MemoryBlock) > (unsigned long)poolStart + poolSizeTotal) {
        panic("Invalid memory address in free");
    }

    if (!validateBlock(block) || block->magic != MAGIC_ALLOC) {
        panic("Double free or corrupted block");
    }

    // Check for double free
    MemoryBlock* check = freeList;
    while (check) {
        if (check == block) {
            panic("Double free detected");
            return;
        }
        check = check->next;
    }

    block->magic = MAGIC_FREE;
    block->next = freeList;
    freeList = block;

    mergeFreeBlocks();
}

static void mergeFreeBlocks() {
    MemoryBlock* curr = freeList;
    while (curr && curr->next) {
        if (!validateBlock(curr) || !validateBlock(curr->next)) {
            panic("Corrupted block during merge");
        }
        
        MemoryBlock* next = curr->next;
        if ((char*)curr + sizeof(MemoryBlock) + curr->size == (char*)next) {
            curr->size += sizeof(MemoryBlock) + next->size;
            curr->next = next->next;
        } else {
            curr = curr->next;
        }
    }
}

static bool validateBlock(MemoryBlock* block) {
    if (block == NULL) return false;

    // Check if block is within our pool boundaries
    if ((unsigned long)block < (unsigned long)poolStart ||
        (unsigned long)block + sizeof(MemoryBlock) > (unsigned long)poolStart + poolSizeTotal) {
        return false;
    }

    return block->magic == MAGIC_ALLOC || block->magic == MAGIC_FREE;
}


static void* physicalToVirtual(uint64_t physical) {
    return (void*)(physical + hhdmOffset);
}

static uint64_t virtualToPhysical(void* virtual) {
    return (uint64_t)virtual - hhdmOffset;
}

size_t getMemTotal() {
    return poolSizeTotal;
}

size_t getMemFree() {
    size_t freeSize = 0;
    MemoryBlock* curr = freeList;
    while (curr != NULL) {
        if (validateBlock(curr) && curr->magic == MAGIC_FREE) {
            freeSize += curr->size;
        }
        curr = curr->next;
    }
    return freeSize;
}

size_t getMemAvailable() {
    return getMemFree();
}

void mm_test() {
    kprint(":: Starting memory test...\n\n", 7);

    kprint("Allocating 256 bytes...\n", 7);
    void* ptr1 = allocateMemory(256);
    if (ptr1 != NULL) {
        kprint("Allocation 1 OK\n", 2);
        freeMemory(ptr1);
        kprint("Free 1 OK\n", 2);
    } else {
        panic("Allocation 1 failed");
    }

    kprint("\nAllocating 512 bytes...\n", 7);
    void* ptr2 = allocateMemory(512);
    void* ptr3 = allocateMemory(512);
    if (ptr2 && ptr3) {
        kprint("Allocation 2-3 OK\n", 2);
        freeMemory(ptr2);
        freeMemory(ptr3);
        kprint("Free 2-3 OK\n", 2);
    } else {
        panic("Allocation 2-3 failed");
    }

    kprint("\nTesting edge cases...\n", 7);
    void* ptr4 = allocateMemory(1024 * 1024); // 1MB allocation
    if (ptr4) {
        kprint("Large allocation OK\n", 2);
        freeMemory(ptr4);
    }

    kprint("\n:: Memory test completed\n", 7);
}

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (char)c;
    }
    return s;
}