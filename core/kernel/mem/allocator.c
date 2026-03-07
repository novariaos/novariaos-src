#include <core/kernel/mem/allocator.h>
#include <core/kernel/mem/buddy.h>
#include <core/kernel/mem/slab.h>
#include <core/kernel/mem/cpu_pool.h>
#include <core/kernel/kstd.h>
#include <log.h>
#include <limine.h>
#include <core/arch/panic.h>
#include <core/arch/smp.h>
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

static size_t alloc_total = 0;
static size_t free_total = 0;
static uintptr_t last_alloc_ptr = 0;
static size_t last_alloc_size = 0;
static uintptr_t last_free_ptr = 0;
static size_t last_free_size = 0;

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

buddy_allocator_t* slab_get_buddy(void) {
    return &buddy_allocator;
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
    slab_init();
    LOG_TRACE("memory_manager_init: completed\n");
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    if (size <= 1024) {
        void* ptr = slab_alloc(size);
        if (ptr) {
            allocated_memory += size;
            alloc_count++;
            return ptr;
        }
    }

    if (size <= 4096 - sizeof(alloc_info_t)) {
        uint32_t cpu_id = smp_current_cpu_id();
        void* block = cpu_pool_alloc(cpu_id, 12);
        if (block) {
            alloc_info_t* info = (alloc_info_t*)block;
            info->order     = 12;
            info->magic     = ALLOC_MAGIC;
            info->user_size = size;

            allocated_memory += size;
            alloc_count++;

            return (void*)((uintptr_t)block + sizeof(alloc_info_t));
        }
    }

    if (size <= 8192 - sizeof(alloc_info_t)) {
        uint32_t cpu_id = smp_current_cpu_id();
        void* block = cpu_pool_alloc(cpu_id, 13);
        if (block) {
            alloc_info_t* info = (alloc_info_t*)block;
            info->order     = 13;
            info->magic     = ALLOC_MAGIC;
            info->user_size = size;

            allocated_memory += size;
            alloc_count++;

            return (void*)((uintptr_t)block + sizeof(alloc_info_t));
        }
    }

    size_t total_size = size + sizeof(alloc_info_t);
    uint32_t order = BUDDY_MIN_ORDER;
    size_t block_size = BUDDY_BLOCK_SIZE(order);

    while (block_size < total_size && order < BUDDY_MAX_ORDER) {
        order++;
        block_size <<= 1;
    }

    void* block = buddy_alloc(&buddy_allocator, block_size);
    if (!block) return NULL;

    alloc_info_t* info = (alloc_info_t*)block;
    info->order     = order;
    info->magic     = ALLOC_MAGIC;
    info->user_size = size;

    allocated_memory += size;
    alloc_count++;

    return (void*)((uintptr_t)block + sizeof(alloc_info_t));
}

void kfree(void* ptr) {
    if (!ptr) return;

    if (slab_owns(ptr)) {
        slab_free(ptr);
        free_count++;
        return;
    }

    alloc_info_t* info = (alloc_info_t*)((uintptr_t)ptr - sizeof(alloc_info_t));

    if (info->magic != ALLOC_MAGIC) {
        LOG_ERROR("kfree: corruption at %p (magic=0x%08x)\n", ptr, info->magic);
        panic("Invalid free: corrupted allocation info");
    }

    if (info->order == 12 || info->order == 13) {
        if (allocated_memory >= info->user_size)
            allocated_memory -= info->user_size;
        free_count++;
        uint32_t cpu_id = smp_current_cpu_id();
        cpu_pool_free(cpu_id, (void*)info, info->order);
        return;
    }

    if (info->order < BUDDY_MIN_ORDER || info->order > BUDDY_MAX_ORDER) {
        LOG_ERROR("kfree: invalid order %u at %p\n", info->order, ptr);
        panic("Invalid free: invalid order");
    }

    if (allocated_memory >= info->user_size)
        allocated_memory -= info->user_size;

    free_count++;
    buddy_free(&buddy_allocator, (void*)info, info->order);
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
    kprint(":: kmalloc memory test\n\n", 7);
    char buf[64];
    int passed = 0;
    int failed = 0;

    static const size_t small_sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    kprint("[1] Small allocations (slab)...\n", 7);
    for (int i = 0; i < 8; i++) {
        size_t sz = small_sizes[i];
        uint8_t* p = (uint8_t*)kmalloc(sz);
        if (!p) { kprint("  FAIL: kmalloc returned NULL\n", 4); failed++; continue; }

        uint8_t pattern = (uint8_t)(sz & 0xFF);
        for (size_t j = 0; j < sz; j++) p[j] = pattern;

        int ok = 1;
        for (size_t j = 0; j < sz; j++) {
            if (p[j] != pattern) { ok = 0; break; }
        }

        kfree(p);
        if (ok) { passed++; } else {
            kprint("  FAIL: pattern mismatch at size ", 4);
            itoa((int)sz, buf, 10); kprint(buf, 4); kprint("\n", 4);
            failed++;
        }
    }

    kprint("[2] Medium allocations (cpu_pool)...\n", 7);
    static const size_t med_sizes[] = {2048, 4000, 5000, 8000};
    for (int i = 0; i < 4; i++) {
        size_t sz = med_sizes[i];
        uint8_t* p = (uint8_t*)kmalloc(sz);
        if (!p) { kprint("  FAIL: NULL\n", 4); failed++; continue; }

        for (size_t j = 0; j < sz; j++) p[j] = (uint8_t)(j & 0xFF);

        int ok = 1;
        for (size_t j = 0; j < sz; j++) {
            if (p[j] != (uint8_t)(j & 0xFF)) { ok = 0; break; }
        }

        kfree(p);
        if (ok) { passed++; } else {
            kprint("  FAIL: pattern mismatch at size ", 4);
            itoa((int)sz, buf, 10); kprint(buf, 4); kprint("\n", 4);
            failed++;
        }
    }

    kprint("[3] Large allocation (buddy)...\n", 7);
    {
        size_t sz = 64 * 1024;
        uint32_t* p = (uint32_t*)kmalloc(sz);
        if (!p) { kprint("  FAIL: NULL\n", 4); failed++; }
        else {
            size_t count = sz / sizeof(uint32_t);
            for (size_t j = 0; j < count; j++) p[j] = (uint32_t)(j * 0xDEAD0001);

            int ok = 1;
            for (size_t j = 0; j < count; j++) {
                if (p[j] != (uint32_t)(j * 0xDEAD0001)) { ok = 0; break; }
            }
            kfree(p);
            if (ok) { passed++; } else { kprint("  FAIL: pattern mismatch\n", 4); failed++; }
        }
    }

    kprint("[4] Stress test (256 alloc/free cycles)...\n", 7);
    {
        int ok = 1;
        for (int i = 0; i < 256; i++) {
            size_t sz = 16 + (i % 240);
            uint8_t* p = (uint8_t*)kmalloc(sz);
            if (!p) { ok = 0; break; }
            p[0] = 0xAA;
            p[sz - 1] = 0x55;
            if (p[0] != 0xAA || p[sz - 1] != 0x55) { ok = 0; kfree(p); break; }
            kfree(p);
        }
        if (ok) { passed++; } else { kprint("  FAIL\n", 4); failed++; }
    }

    kprint("[5] Simultaneous allocations...\n", 7);
    {
        #define SIM_COUNT 16
        void* ptrs[SIM_COUNT];
        int ok = 1;

        for (int i = 0; i < SIM_COUNT; i++) {
            ptrs[i] = kmalloc(128);
            if (!ptrs[i]) { ok = 0; break; }
            memset(ptrs[i], i & 0xFF, 128);
        }

        for (int i = 0; i < SIM_COUNT && ok; i++) {
            if (!ptrs[i]) continue;
            uint8_t* p = (uint8_t*)ptrs[i];
            for (int j = 0; j < 128; j++) {
                if (p[j] != (uint8_t)(i & 0xFF)) { ok = 0; break; }
            }
        }

        for (int i = 0; i < SIM_COUNT; i++) {
            if (ptrs[i]) kfree(ptrs[i]);
        }
        if (ok) { passed++; } else { kprint("  FAIL: overlap or corruption\n", 4); failed++; }
        #undef SIM_COUNT
    }

    kprint("[6] Edge cases...\n", 7);
    {
        void* p = kmalloc(0);
        int ok = (p == NULL);
        kfree(NULL);
        if (ok) { passed++; } else { kprint("  FAIL: kmalloc(0) != NULL\n", 4); failed++; }
    }

    kprint("\n:: Results: ", 7);
    itoa(passed, buf, 10); kprint(buf, 2);
    kprint(" passed, ", 7);
    itoa(failed, buf, 10); kprint(buf, failed ? 4 : 2);
    kprint(" failed\n\n", 7);

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
        LOG_ERROR("check_memory_leaks: leak detected! %zu unfreed allocation (s)\n",
                 (size_t)alloc_count - (size_t)free_count);
        kprint("Memory leak detected! ", 4);
        itoa((int)alloc_count - (int)free_count, buffer, 10);
        kprint(buffer, 4);
        kprint(" unfreed allocation (s)\n", 4);
    }
}