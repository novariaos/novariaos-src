#include <core/kernel/mem/allocator.h>
#include <core/kernel/mem/buddy.h>
#include <core/kernel/kstd.h>
#include <core/kernel/log.h>
#include <lib/bootloader/limine.h>
#include <core/arch/panic.h>
#include <stddef.h>
#include <stdint.h>

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

static buddy_allocator_t buddy_allocator;
static uint64_t hhdm_offset;
static size_t allocated_memory = 0;
static size_t alloc_count = 0;
static size_t free_count = 0;

typedef struct alloc_info {
    uint32_t order;
    uint32_t magic;
    size_t user_size;
} alloc_info_t;

#define ALLOC_MAGIC 0xA110C123

void format_memory_size(size_t size, char* buffer) {
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

void memory_manager_init(void) {
    LOG_TRACE("memory_manager_init: starting initialization\n");

    if (hhdm_request.response == NULL) {
        LOG_ERROR("memory_manager_init: HHDM request failed\n");
        panic("HHDM request failed");
        return;
    }
    hhdm_offset = hhdm_request.response->offset;
    LOG_TRACE("memory_manager_init: HHDM offset = %llu\n", hhdm_offset);

    if (memmap_request.response == NULL) {
        LOG_ERROR("memory_manager_init: Memory map request failed\n");
        panic("Memory map request failed");
        return;
    }

    struct limine_memmap_response* memmap = memmap_request.response;
    LOG_TRACE("memory_manager_init: found %zu memory map entries\n", memmap->entry_count);

    size_t max_usable_size = 0;
    struct limine_memmap_entry* best_entry = NULL;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        LOG_TRACE("memory_manager_init: entry %zu: base=%llx, length=%llx, type=%u\n",
                 i, entry->base, entry->length, entry->type);

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            if (entry->length > max_usable_size) {
                max_usable_size = entry->length;
                best_entry = entry;
            }
        }
    }

    if (best_entry == NULL) {
        LOG_ERROR("memory_manager_init: No suitable memory region found\n");
        panic("No suitable memory region found");
        return;
    }

    LOG_TRACE("memory_manager_init: selected memory region: base=%llx, length=%llx\n",
             best_entry->base, best_entry->length);

    void* pool_start = (void*)(best_entry->base + hhdm_offset);
    size_t pool_size = best_entry->length;

    LOG_TRACE("memory_manager_init: pool_start=%p, pool_size=%zu\n", pool_start, pool_size);

    buddy_init(&buddy_allocator, pool_start, pool_size, hhdm_offset);

    char buffer[64];
    format_memory_size(buddy_get_total_memory(&buddy_allocator), buffer);
    LOG_INFO("Buddy allocator initialized (%s)\n", buffer);
    LOG_TRACE("memory_manager_init: completed\n");
}

void* kmalloc(size_t size) {
    LOG_TRACE("kmalloc: requested size=%zu\n", size);

    if (size == 0) {
        LOG_TRACE("kmalloc: zero size, returning NULL\n");
        return NULL;
    }

    size_t total_size = size + sizeof(alloc_info_t);
    uint32_t order = BUDDY_MIN_ORDER;
    size_t block_size = BUDDY_BLOCK_SIZE(order);

    while (block_size < total_size && order < BUDDY_MAX_ORDER) {
        order++;
        block_size <<= 1;
    }

    LOG_TRACE("kmalloc: calculated order=%u for total_size=%zu\n", order, total_size);

    void* block = buddy_alloc(&buddy_allocator, block_size);
    if (!block) {
        LOG_TRACE("kmalloc: buddy_alloc failed for size %zu\n", block_size);
        return NULL;
    }

    alloc_info_t* info = (alloc_info_t*)block;
    info->order = order;
    info->magic = ALLOC_MAGIC;
    info->user_size = size;

    LOG_TRACE("kmalloc: allocated block at %p (order=%u, user_size=%zu)\n", block, order, size);

    allocated_memory += size;
    alloc_count++;

    return (void*)((uintptr_t)block + sizeof(alloc_info_t));
}

void kfree(void* ptr) {
    LOG_TRACE("kfree: freeing ptr=%p\n", ptr);

    if (!ptr) {
        LOG_TRACE("kfree: NULL pointer, ignoring\n");
        return;
    }

    alloc_info_t* info = (alloc_info_t*)((uintptr_t)ptr - sizeof(alloc_info_t));

    if (info->magic != ALLOC_MAGIC) {
        LOG_ERROR("kfree: corrupted allocation info at %p (expected magic=0x%08x, got=0x%08x)\n",
                 ptr, ALLOC_MAGIC, info->magic);
        panic("Invalid free: corrupted allocation info");
    }

    if (info->order < BUDDY_MIN_ORDER || info->order > BUDDY_MAX_ORDER) {
        LOG_ERROR("kfree: invalid order %u in allocation info (ptr=%p)\n", info->order, ptr);
        panic("Invalid free: invalid order in allocation info");
    }

    LOG_TRACE("kfree: freeing block order=%u, user_size=%zu\n", info->order, info->user_size);

    if (allocated_memory >= info->user_size) {
        allocated_memory -= info->user_size;
    } else {
        LOG_WARN("kfree: allocated_memory (%zu) < user_size (%zu), possible corruption\n",
                allocated_memory, info->user_size);
    }
    free_count++;

    buddy_free(&buddy_allocator, (void*)info, info->order);
    LOG_TRACE("kfree: completed freeing ptr=%p\n", ptr);
}

size_t get_memory_total(void) {
    return buddy_get_total_memory(&buddy_allocator);
}

size_t get_memory_free(void) {
    return buddy_get_free_memory(&buddy_allocator);
}

size_t get_memory_used(void) {
    return allocated_memory;
}

size_t get_memory_available(void) {
    return get_memory_free();
}

void memory_test(void) {
    LOG_TRACE("memory_test: starting memory tests\n");
    kprint(":: Starting buddy memory test...\n\n", 7);

    char buffer[64];
    format_memory_size(buddy_get_free_memory(&buddy_allocator), buffer);
    kprint("Initial free memory: ", 7);
    kprint(buffer, 7);
    kprint("\n", 7);

    LOG_TRACE("memory_test: allocating 256 bytes\n");
    kprint("Allocating 256 bytes...\n", 7);
    void* ptr1 = kmalloc(256);
    if (ptr1 != NULL) {
        LOG_TRACE("memory_test: allocation 1 successful at %p\n", ptr1);
        kprint("Allocation 1 OK\n", 2);

        format_memory_size(buddy_get_free_memory(&buddy_allocator), buffer);
        kprint("Free memory after alloc1: ", 7);
        kprint(buffer, 7);
        kprint("\n", 7);

        LOG_TRACE("memory_test: freeing ptr1=%p\n", ptr1);
        kfree(ptr1);
        kprint("Free 1 OK\n", 2);

        format_memory_size(buddy_get_free_memory(&buddy_allocator), buffer);
        kprint("Free memory after free1: ", 7);
        kprint(buffer, 7);
        kprint("\n", 7);
    } else {
        LOG_ERROR("memory_test: allocation 1 failed\n");
        panic("Allocation 1 failed");
    }

    kprint("\nAllocating 512 bytes...\n", 7);
    void* ptr2 = kmalloc(512);
    format_memory_size(buddy_get_free_memory(&buddy_allocator), buffer);
    kprint("Free memory after alloc2: ", 7);
    kprint(buffer, 7);
    kprint("\n", 7);

    void* ptr3 = kmalloc(512);
    if (ptr2 && ptr3) {
        kprint("Allocation 2-3 OK\n", 2);
        kfree(ptr2);
        kfree(ptr3);
        kprint("Free 2-3 OK\n", 2);
    } else {
        format_memory_size(buddy_get_free_memory(&buddy_allocator), buffer);
        kprint("Free memory when failed: ", 4);
        kprint(buffer, 4);
        kprint("\n", 4);
        panic("Allocation 2-3 failed");
    }

    kprint("\nTesting edge cases...\n", 7);
    void* ptr4 = kmalloc(1024 * 1024);
    if (ptr4) {
        kprint("Large allocation OK\n", 2);
        kfree(ptr4);
    }

    kprint("\n:: Buddy memory test completed\n", 7);

    check_memory_leaks();
}

void check_memory_leaks(void) {
    char buffer[128];

    LOG_TRACE("check_memory_leaks: alloc_count=%zu, free_count=%zu, allocated_memory=%zu\n",
             alloc_count, free_count, allocated_memory);

    kprint(":: Memory leak check\n", 7);

    kprint("Allocated memory: ", 7);
    format_memory_size(allocated_memory, buffer);
    kprint(buffer, 7);
    kprint("\n", 7);

    kprint("Alloc count: ", 7);
    itoa(alloc_count, buffer, 10);
    kprint(buffer, 7);
    kprint("\n", 7);

    kprint("Free count: ", 7);
    itoa(free_count, buffer, 10);
    kprint(buffer, 7);
    kprint("\n", 7);

    if (alloc_count == free_count) {
        LOG_TRACE("check_memory_leaks: no leaks detected\n");
        kprint("No memory leaks detected\n", 2);
    } else {
        LOG_ERROR("check_memory_leaks: leak detected! %zu unfreed allocations\n",
                 (size_t)alloc_count - (size_t)free_count);
        kprint("Memory leak detected! ", 4);
        itoa((int)alloc_count - (int)free_count, buffer, 10);
        kprint(buffer, 4);
        kprint(" unfreed allocations\n", 4);
    }
}
