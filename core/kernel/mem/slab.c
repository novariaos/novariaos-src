#include <core/kernel/mem/slab.h>
#include <core/kernel/mem/buddy.h>
#include <core/kernel/kstd.h>
#include <log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

extern buddy_allocator_t* slab_get_buddy(void);

typedef struct slab_obj {
    struct slab_obj* next;
} slab_obj_t;

typedef struct slab_page {
    struct slab_page* next;
    struct slab_page* prev;
    uint32_t          magic;
    uint16_t          obj_size;
    uint16_t          total_count;
    uint16_t          free_count;
    uint16_t          _pad;
    slab_obj_t*       freelist;
} slab_page_t;

typedef struct {
    uint16_t     obj_size;
    slab_page_t* partial;
    slab_page_t* full;
} slab_cache_t;

static slab_cache_t caches[SLAB_SIZES_COUNT];
static const size_t obj_sizes[SLAB_SIZES_COUNT] = SLAB_OBJ_SIZES;

static void list_remove(slab_page_t** head, slab_page_t* page) {
    if (page->prev)
        page->prev->next = page->next;
    else
        *head = page->next;
    if (page->next)
        page->next->prev = page->prev;
    page->next = NULL;
    page->prev = NULL;
}

static void list_push(slab_page_t** head, slab_page_t* page) {
    page->prev = NULL;
    page->next = *head;
    if (*head)
        (*head)->prev = page;
    *head = page;
}

static void slab_page_init(slab_page_t* page, size_t obj_size) {
    page->magic      = SLAB_MAGIC;
    page->obj_size   = (uint16_t)obj_size;
    page->next       = NULL;
    page->prev       = NULL;
    page->freelist   = NULL;

    uintptr_t start  = (uintptr_t)page + sizeof(slab_page_t);
    uintptr_t end    = (uintptr_t)page + SLAB_PAGE_SIZE;
    uint16_t  count  = 0;

    for (uintptr_t p = start; p + obj_size <= end; p += obj_size) {
        slab_obj_t* obj = (slab_obj_t*)p;
        obj->next       = page->freelist;
        page->freelist  = obj;
        count++;
    }
    page->total_count = count;
    page->free_count  = count;
}

static slab_page_t* slab_new_page(slab_cache_t* cache) {
    buddy_allocator_t* buddy = slab_get_buddy();
    void* raw = buddy_alloc(buddy, SLAB_PAGE_SIZE);
    if (!raw) return NULL;
    slab_page_t* page = (slab_page_t*)raw;
    slab_page_init(page, cache->obj_size);
    list_push(&cache->partial, page);
    return page;
}

static slab_cache_t* cache_for_page(slab_page_t* page) {
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        if (caches[i].obj_size == page->obj_size)
            return &caches[i];
    }
    return NULL;
}

void slab_init(void) {
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        caches[i].obj_size = (uint16_t)obj_sizes[i];
        caches[i].partial  = NULL;
        caches[i].full     = NULL;
    }
    LOG_INFO("slab: initialized %d caches\n", SLAB_SIZES_COUNT);
}

void* slab_alloc(size_t size) {
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        if (size > caches[i].obj_size) continue;

        slab_cache_t* cache = &caches[i];
        slab_page_t* page = cache->partial;

        if (!page) {
            page = slab_new_page(cache);
            if (!page) return NULL;
        }

        slab_obj_t* obj = page->freelist;
        page->freelist  = obj->next;
        page->free_count--;

        if (page->free_count == 0) {
            list_remove(&cache->partial, page);
            list_push(&cache->full, page);
        }

        return (void*)obj;
    }
    return NULL;
}

void slab_free(void* ptr) {
    if (!ptr) return;

    uintptr_t page_addr = (uintptr_t)ptr & ~(uintptr_t)(SLAB_PAGE_SIZE - 1);
    slab_page_t* page   = (slab_page_t*)page_addr;

    if (page->magic != SLAB_MAGIC) {
        LOG_WARN("slab_free: bad magic at %p, not a slab page\n", page);
        return;
    }

    bool was_full = (page->free_count == 0);

    slab_obj_t* obj = (slab_obj_t*)ptr;
    obj->next       = page->freelist;
    page->freelist  = obj;
    page->free_count++;

    if (was_full) {
        slab_cache_t* cache = cache_for_page(page);
        if (cache) {
            list_remove(&cache->full, page);
            list_push(&cache->partial, page);
        }
    }
}

int slab_owns(void* ptr) {
    if (!ptr) return 0;
    uintptr_t page_addr = (uintptr_t)ptr & ~(uintptr_t)(SLAB_PAGE_SIZE - 1);
    slab_page_t* page   = (slab_page_t*)page_addr;
    return (page->magic == SLAB_MAGIC);
}

typedef struct {
    slab_page_t* partial;
    slab_page_t* full;
    uint16_t     obj_size;
} cpu_slab_cache_t;

typedef struct {
    cpu_slab_cache_t caches[SLAB_SIZES_COUNT];
    uint8_t          initialized;
} cpu_slab_t;

static cpu_slab_t cpu_slabs[SLAB_MAX_CPUS];

void slab_cpu_init(uint32_t cpu_id) {
    if (cpu_id >= SLAB_MAX_CPUS) return;
    cpu_slab_t* cs = &cpu_slabs[cpu_id];
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        cs->caches[i].obj_size = (uint16_t)obj_sizes[i];
        cs->caches[i].partial  = NULL;
        cs->caches[i].full     = NULL;
    }
    cs->initialized = 1;
}

static slab_page_t* cpu_slab_new_page(cpu_slab_cache_t* cache) {
    buddy_allocator_t* buddy = slab_get_buddy();
    void* raw = buddy_alloc(buddy, SLAB_PAGE_SIZE);
    if (!raw) return NULL;
    slab_page_t* page = (slab_page_t*)raw;
    slab_page_init(page, cache->obj_size);
    list_push(&cache->partial, page);
    return page;
}

void* slab_alloc_cpu(uint32_t cpu_id, size_t size) {
    if (cpu_id >= SLAB_MAX_CPUS) return slab_alloc(size);
    cpu_slab_t* cs = &cpu_slabs[cpu_id];
    if (!cs->initialized) return slab_alloc(size);

    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        if (size > cs->caches[i].obj_size) continue;
        cpu_slab_cache_t* cache = &cs->caches[i];

        slab_page_t* page = cache->partial;

        if (!page) {
            page = cpu_slab_new_page(cache);
            if (!page) return NULL;
        }

        slab_obj_t* obj = page->freelist;
        page->freelist  = obj->next;
        page->free_count--;

        if (page->free_count == 0) {
            list_remove(&cache->partial, page);
            list_push(&cache->full, page);
        }

        return (void*)obj;
    }
    return NULL;
}

void slab_free_cpu(void* ptr) {
    slab_free(ptr);
}
