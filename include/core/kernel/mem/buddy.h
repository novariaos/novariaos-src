#ifndef BUDDY_H
#define BUDDY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <core/arch/spinlock.h>

// Configuration constants
#define BUDDY_MAX_ORDER 28  /**< Maximum allocation order (2^28 = 256MB blocks) */
#define BUDDY_MIN_ORDER 12  /**< Minimum allocation order (2^12 = 4KB blocks) */

#define BUDDY_BLOCK_SIZE(order) (1ULL << (order))
#define BUDDY_TOTAL_BLOCKS_IN_POOL(order, pool_size) (pool_size / BUDDY_BLOCK_SIZE(order))
#define BUDDY_PAGE_SIZE BUDDY_BLOCK_SIZE(BUDDY_MIN_ORDER)
#define BUDDY_BITMAP_SIZE(order, pool_size) ((BUDDY_TOTAL_BLOCKS_IN_POOL(order, pool_size) + 31) / 32)

typedef struct buddy_allocator {
    void* pool_start;                                   /**< Start address of memory pool */
    size_t pool_size;                                   /**< Total size of memory pool */
    uint64_t hhdm_offset;                               /**< Higher-half direct mapping offset */
    uint32_t* free_area_bitmap[BUDDY_MAX_ORDER + 1];    /**< Bitmaps tracking free blocks per order */
    size_t free_area_size[BUDDY_MAX_ORDER + 1];         /**< Number of free blocks per order */
    size_t max_blocks[BUDDY_MAX_ORDER + 1];             /**< Maximum possible blocks per order */
    spinlock_t lock;                                    /**< Thread synchronization lock */
} buddy_allocator_t;

void buddy_init(buddy_allocator_t* allocator, void* pool_start, size_t pool_size, uint64_t hhdm_offset);
void* buddy_alloc(buddy_allocator_t* allocator, size_t size);
void buddy_free(buddy_allocator_t* allocator, void* ptr, uint32_t order);
size_t buddy_get_free_memory(buddy_allocator_t* allocator);
size_t buddy_get_total_memory(buddy_allocator_t* allocator);

#endif
