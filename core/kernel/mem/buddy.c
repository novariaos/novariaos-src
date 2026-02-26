#include <core/kernel/mem/buddy.h>
#include <log.h>
#include <core/arch/panic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static inline uint32_t get_minimum_order(size_t size) {
    uint32_t order = BUDDY_MIN_ORDER;
    size_t block_size = BUDDY_BLOCK_SIZE(order);

    while (block_size < size && order < BUDDY_MAX_ORDER) {
        order++;
        block_size <<= 1;
    }

    return order;
}

static inline void* get_buddy_address(void* block, uint32_t order, void* pool_start) {
    uintptr_t block_addr = (uintptr_t)block;
    uintptr_t pool_addr = (uintptr_t)pool_start;
    uintptr_t offset = block_addr - pool_addr;
    uintptr_t buddy_offset = offset ^ BUDDY_BLOCK_SIZE(order);

    return (void*)(pool_addr + buddy_offset);
}

static inline bool is_valid_block(buddy_allocator_t* allocator, void* ptr, uint32_t order) {
    if (!ptr) return false;

    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t pool_start = (uintptr_t)allocator->pool_start;
    uintptr_t pool_end = pool_start + allocator->pool_size;

    if (addr < pool_start || addr >= pool_end) return false;

    size_t offset = addr - pool_start;
    if (offset % BUDDY_BLOCK_SIZE(order) != 0) return false;

    return true;
}

static inline size_t get_block_index(void* ptr, void* pool_start, uint32_t order) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t pool_addr = (uintptr_t)pool_start;
    size_t offset = addr - pool_addr;
    return offset / BUDDY_BLOCK_SIZE(order);
}

static inline bool is_valid_bitmap_index(uint32_t* bitmap, size_t max_blocks, size_t index) {
    if (!bitmap || index >= max_blocks) {
        return false;
    }
    return true;
}

static inline void set_bit(uint32_t* bitmap, size_t index, size_t max_blocks) {
    if (!is_valid_bitmap_index(bitmap, max_blocks, index)) {
        LOG_ERROR("set_bit: invalid index %zu (max_blocks=%zu)\n", index, max_blocks);
        return;
    }
    bitmap[index / 32] |= (1U << (index % 32));
}

static inline void clear_bit(uint32_t* bitmap, size_t index, size_t max_blocks) {
    if (!is_valid_bitmap_index(bitmap, max_blocks, index)) {
        LOG_ERROR("clear_bit: invalid index %zu (max_blocks=%zu)\n", index, max_blocks);
        return;
    }
    bitmap[index / 32] &= ~(1U << (index % 32));
}

static inline bool test_bit(uint32_t* bitmap, size_t index, size_t max_blocks) {
    if (!is_valid_bitmap_index(bitmap, max_blocks, index)) {
        LOG_ERROR("test_bit: invalid index %zu (max_blocks=%zu)\n", index, max_blocks);
        return false;
    }
    return (bitmap[index / 32] & (1U << (index % 32))) != 0;
}

static inline size_t find_first_free_bit(uint32_t* bitmap, size_t max_blocks) {
    for (size_t i = 0; i < max_blocks; i++) {
        if (!test_bit(bitmap, i, max_blocks)) {
            return i;
        }
    }
    return (size_t)-1;
}

static void* get_block_address(buddy_allocator_t* allocator, uint32_t order, size_t index) {
    return (void*)((uintptr_t)allocator->pool_start + index * BUDDY_BLOCK_SIZE(order));
}

static void split_block(buddy_allocator_t* allocator, uint32_t order, size_t index) {
    uint32_t new_order = order - 1;

    size_t left_index = index * 2;
    size_t right_index = index * 2 + 1;

    if (left_index >= allocator->max_blocks[new_order] ||
        right_index >= allocator->max_blocks[new_order]) {
        LOG_ERROR("split_block: invalid indices for new order %u: left=%zu, right=%zu, max=%zu\n",
                 new_order, left_index, right_index, allocator->max_blocks[new_order]);
        return;
    }

    clear_bit(allocator->free_area_bitmap[new_order], left_index, allocator->max_blocks[new_order]);
    clear_bit(allocator->free_area_bitmap[new_order], right_index, allocator->max_blocks[new_order]);
    allocator->free_area_size[new_order] += 2;

    LOG_TRACE("split_block: split order %u block %zu into order %u blocks %zu and %zu\n",
             order, index, new_order, left_index, right_index);
}

static void* alloc_block(buddy_allocator_t* allocator, uint32_t order) {
    LOG_TRACE("alloc_block: attempting to allocate order=%u\n", order);

    if (order > BUDDY_MAX_ORDER) {
        LOG_TRACE("alloc_block: order %u > max_order %u, returning NULL\n", order, BUDDY_MAX_ORDER);
        return NULL;
    }

    if (allocator->free_area_size[order] == 0) {
        LOG_TRACE("alloc_block: no free blocks of order %u, trying larger order %u\n", order, order + 1);
        void* larger_block = alloc_block(allocator, order + 1);
        if (!larger_block) {
            LOG_TRACE("alloc_block: failed to allocate larger block\n");
            return NULL;
        }

        size_t larger_index = get_block_index(larger_block, allocator->pool_start, order + 1);
        LOG_TRACE("alloc_block: splitting block at index %zu (order %u) into order %u\n", larger_index, order + 1, order);
        split_block(allocator, order + 1, larger_index);

        size_t left_index = larger_index * 2;

        if (left_index >= allocator->max_blocks[order]) {
            LOG_ERROR("alloc_block: invalid left_index %zu for order %u (max_blocks=%zu)\n",
                     left_index, order, allocator->max_blocks[order]);
            size_t right_index = larger_index * 2 + 1;
            if (right_index >= allocator->max_blocks[order]) {
                LOG_ERROR("alloc_block: both split indices invalid, returning larger block\n");
                return larger_block;
            }
            left_index = right_index;
        }

        set_bit(allocator->free_area_bitmap[order], left_index, allocator->max_blocks[order]);
        allocator->free_area_size[order]--;

        void* result = get_block_address(allocator, order, left_index);
        LOG_TRACE("alloc_block: allocated split block at %p (index %zu)\n", result, left_index);
        return result;
    }

    size_t free_index = find_first_free_bit(allocator->free_area_bitmap[order],
                                           allocator->max_blocks[order]);
    if (free_index == (size_t)-1) {
        LOG_TRACE("alloc_block: no free blocks found in bitmap for order %u\n", order);
        return NULL;
    }

    set_bit(allocator->free_area_bitmap[order], free_index, allocator->max_blocks[order]);
    allocator->free_area_size[order]--;

    void* result = get_block_address(allocator, order, free_index);
    LOG_TRACE("alloc_block: allocated existing block at %p (index %zu)\n", result, free_index);
    return result;
}

static void free_block(buddy_allocator_t* allocator, void* ptr, uint32_t order) {
    LOG_TRACE("free_block: freeing block at %p (order %u)\n", ptr, order);

    if (!is_valid_block(allocator, ptr, order)) {
        LOG_ERROR("free_block: invalid block %p (order %u)\n", ptr, order);
        return;
    }

    size_t index = get_block_index(ptr, allocator->pool_start, order);

    clear_bit(allocator->free_area_bitmap[order], index, allocator->max_blocks[order]);
    allocator->free_area_size[order]++;
    LOG_TRACE("free_block: marked block free at index %zu (order %u)\n", index, order);

    void* current_block = ptr;
    uint32_t current_order = order;

    while (current_order < BUDDY_MAX_ORDER) {
        void* buddy = get_buddy_address(current_block, current_order, allocator->pool_start);
        LOG_TRACE("free_block: checking buddy at %p (order %u)\n", buddy, current_order);

        if (!is_valid_block(allocator, buddy, current_order)) {
            LOG_TRACE("free_block: buddy not valid, stopping merge\n");
            break;
        }

        size_t buddy_index = get_block_index(buddy, allocator->pool_start, current_order);
        if (test_bit(allocator->free_area_bitmap[current_order], buddy_index, allocator->max_blocks[current_order])) {
            LOG_TRACE("free_block: buddy at index %zu is allocated, stopping merge\n", buddy_index);
            break;
        }

        LOG_TRACE("free_block: merging blocks into order %u\n", current_order + 1);

        if (test_bit(allocator->free_area_bitmap[current_order], index, allocator->max_blocks[current_order]) ||
            test_bit(allocator->free_area_bitmap[current_order], buddy_index, allocator->max_blocks[current_order])) {
            LOG_ERROR("free_block: one or both blocks not free during merge (index=%zu, buddy_index=%zu)\n",
                     index, buddy_index);
            break;
        }

        set_bit(allocator->free_area_bitmap[current_order], index, allocator->max_blocks[current_order]);
        set_bit(allocator->free_area_bitmap[current_order], buddy_index, allocator->max_blocks[current_order]);
        allocator->free_area_size[current_order] -= 2;

        void* merged_block = (uintptr_t)current_block < (uintptr_t)buddy ? current_block : buddy;
        size_t merged_index = get_block_index(merged_block, allocator->pool_start, current_order + 1);

        if (merged_index >= allocator->max_blocks[current_order + 1]) {
            LOG_ERROR("free_block: invalid merged_index %zu for order %u\n", merged_index, current_order + 1);
            break;
        }

        clear_bit(allocator->free_area_bitmap[current_order + 1], merged_index, allocator->max_blocks[current_order + 1]);
        allocator->free_area_size[current_order + 1]++;

        LOG_TRACE("free_block: merged into block at index %zu (order %u)\n", merged_index, current_order + 1);

        current_block = merged_block;
        current_order++;
        index = merged_index;
    }

    LOG_TRACE("free_block: completed freeing block\n");
}

void buddy_init(buddy_allocator_t* allocator, void* pool_start, size_t pool_size, uint64_t hhdm_offset) {
    LOG_TRACE("buddy_init: pool_start=%p, pool_size=%zu, hhdm_offset=%llu\n", pool_start, pool_size, hhdm_offset);

    if (!allocator) {
        LOG_ERROR("buddy_init: allocator pointer is NULL\n");
        panic("Buddy allocator initialization failed: NULL allocator");
    }

    if (!pool_start) {
        LOG_ERROR("buddy_init: pool_start is NULL\n");
        panic("Buddy allocator initialization failed: NULL pool_start");
    }

    if (pool_size == 0) {
        LOG_ERROR("buddy_init: pool_size is zero\n");
        panic("Buddy allocator initialization failed: zero pool_size");
    }

    if (pool_size < BUDDY_BLOCK_SIZE(BUDDY_MIN_ORDER)) {
        LOG_ERROR("buddy_init: pool_size %zu too small (minimum %zu)\n",
                 pool_size, BUDDY_BLOCK_SIZE(BUDDY_MIN_ORDER));
        panic("Buddy allocator initialization failed: pool too small");
    }

    spinlock_init(&allocator->lock);
    spinlock_acquire(&allocator->lock);

    allocator->pool_start = pool_start;
    allocator->pool_size = pool_size;
    allocator->hhdm_offset = hhdm_offset;

    if (allocator->pool_size % BUDDY_BLOCK_SIZE(BUDDY_MIN_ORDER) != 0) {
        allocator->pool_size -= allocator->pool_size % BUDDY_BLOCK_SIZE(BUDDY_MIN_ORDER);
    }

    size_t preliminary_max_blocks[BUDDY_MAX_ORDER + 1];
    for (uint32_t order = BUDDY_MIN_ORDER; order <= BUDDY_MAX_ORDER; order++) {
        preliminary_max_blocks[order] = allocator->pool_size / BUDDY_BLOCK_SIZE(order);
    }

    size_t total_bitmap_size = 0;
    for (uint32_t order = BUDDY_MIN_ORDER; order <= BUDDY_MAX_ORDER; order++) {
        size_t bitmap_words = (preliminary_max_blocks[order] + 31) / 32;
        total_bitmap_size += bitmap_words * sizeof(uint32_t);
    }

    if (allocator->pool_size > total_bitmap_size) {
        allocator->pool_size -= total_bitmap_size;
    } else {
        LOG_ERROR("buddy_init: pool too small for bitmaps (pool_size=%zu, bitmap_size=%zu)\n",
                 allocator->pool_size, total_bitmap_size);
        panic("Pool too small for buddy allocator bitmaps");
    }

    for (uint32_t order = BUDDY_MIN_ORDER; order <= BUDDY_MAX_ORDER; order++) {
        allocator->max_blocks[order] = allocator->pool_size / BUDDY_BLOCK_SIZE(order);
    }

    uintptr_t bitmap_start = (uintptr_t)allocator->pool_start + allocator->pool_size;
    for (int32_t order = BUDDY_MAX_ORDER; order >= BUDDY_MIN_ORDER; order--) {
        size_t bitmap_words = (allocator->max_blocks[order] + 31) / 32;
        bitmap_start -= bitmap_words * sizeof(uint32_t);
        allocator->free_area_bitmap[order] = (uint32_t*)bitmap_start;
        allocator->free_area_size[order] = 0;

        for (size_t i = 0; i < bitmap_words; i++) {
            allocator->free_area_bitmap[order][i] = 0xFFFFFFFF;
        }
    }

    if (bitmap_start < (uintptr_t)allocator->pool_start) {
        LOG_ERROR("buddy_init: bitmap placement error\n");
        panic("Bitmap placement error in buddy allocator");
    }

    uint32_t max_order = BUDDY_MAX_ORDER;
    while (max_order > BUDDY_MIN_ORDER && allocator->max_blocks[max_order] == 0) {
        max_order--;
    }

    if (max_order < BUDDY_MIN_ORDER || allocator->max_blocks[max_order] == 0) {
        LOG_ERROR("buddy_init: no suitable order found for pool_size=%zu\n", allocator->pool_size);
        panic("No suitable order found for buddy allocator");
    }

    size_t block_size = BUDDY_BLOCK_SIZE(max_order);
    if (allocator->pool_size % block_size != 0) {
        LOG_WARN("buddy_init: pool_size %zu not divisible by block_size %zu, adjusting\n",
                allocator->pool_size, block_size);
        allocator->pool_size = (allocator->pool_size / block_size) * block_size;
        allocator->max_blocks[max_order] = allocator->pool_size / block_size;
    }

    for (size_t i = 0; i < allocator->max_blocks[max_order]; i++) {
        clear_bit(allocator->free_area_bitmap[max_order], i, allocator->max_blocks[max_order]);
        allocator->free_area_size[max_order]++;
        LOG_TRACE("buddy_init: marked block %zu in order max_order %u as free\n", i, max_order);
    }

    spinlock_release(&allocator->lock);
    LOG_TRACE("buddy_init: completed, final pool_size=%zu\n", allocator->pool_size);
}

void* buddy_alloc(buddy_allocator_t* allocator, size_t size) {
    LOG_TRACE("buddy_alloc: requested size=%zu\n", size);

    if (!allocator) {
        LOG_ERROR("buddy_alloc: allocator is NULL\n");
        return NULL;
    }

    if (size == 0) {
        LOG_TRACE("buddy_alloc: zero size requested\n");
        return NULL;
    }

    if (size > allocator->pool_size) {
        LOG_TRACE("buddy_alloc: size %zu > pool_size %zu\n", size, allocator->pool_size);
        return NULL;
    }

    spinlock_acquire(&allocator->lock);
    uint32_t target_order = get_minimum_order(size);
    void* result = alloc_block(allocator, target_order);
    spinlock_release(&allocator->lock);

    LOG_TRACE("buddy_alloc: allocated at %p (order=%u)\n", result, target_order);
    return result;
}

void buddy_free(buddy_allocator_t* allocator, void* ptr, uint32_t order) {
    LOG_TRACE("buddy_free: ptr=%p, order=%u\n", ptr, order);

    if (!allocator) {
        LOG_ERROR("buddy_free: allocator is NULL\n");
        return;
    }

    if (!ptr) {
        LOG_TRACE("buddy_free: NULL pointer, ignoring\n");
        return;
    }

    if (order < BUDDY_MIN_ORDER || order > BUDDY_MAX_ORDER) {
        LOG_ERROR("buddy_free: invalid order %u (must be %u-%u)\n", order, BUDDY_MIN_ORDER, BUDDY_MAX_ORDER);
        return;
    }

    if (!is_valid_block(allocator, ptr, order)) {
        LOG_ERROR("buddy_free: invalid block ptr=%p order=%u\n", ptr, order);
        return;
    }

    spinlock_acquire(&allocator->lock);
    free_block(allocator, ptr, order);
    spinlock_release(&allocator->lock);

    LOG_TRACE("buddy_free: freed block at %p\n", ptr);
}

size_t buddy_get_free_memory(buddy_allocator_t* allocator) {
    spinlock_acquire(&allocator->lock);
    size_t free_memory = 0;

    for (uint32_t order = BUDDY_MIN_ORDER; order <= BUDDY_MAX_ORDER; order++) {
        free_memory += allocator->free_area_size[order] * BUDDY_BLOCK_SIZE(order);
    }

    spinlock_release(&allocator->lock);
    LOG_TRACE("buddy_get_free_memory: %zu bytes free\n", free_memory);
    return free_memory;
}

size_t buddy_get_total_memory(buddy_allocator_t* allocator) {
    size_t total = allocator->pool_size;
    LOG_TRACE("buddy_get_total_memory: %zu bytes total\n", total);
    return total;
}
