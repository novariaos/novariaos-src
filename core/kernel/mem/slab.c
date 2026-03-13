#include <core/kernel/mem/slab.h>
#include <core/kernel/mem/buddy.h>
#include <core/kernel/kstd.h>
#include <core/arch/spinlock.h>
#include <core/arch/smp.h>
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
    uint16_t          owner_cpu;
    slab_obj_t*       freelist;
} slab_page_t;

#define SLAB_CPU_NONE 0xFFFF

typedef struct {
    uint16_t     obj_size;
    slab_page_t* partial;
    slab_page_t* full;
    spinlock_t   lock;
} slab_cache_t;

typedef struct {
    slab_page_t* partial;
    slab_page_t* full;
    uint16_t     obj_size;
} cpu_cache_t;

typedef struct {
    cpu_cache_t caches[SLAB_SIZES_COUNT];
    uint8_t     initialized;
} cpu_slab_t;

static slab_cache_t caches[SLAB_SIZES_COUNT];
static cpu_slab_t   cpu_slabs[SLAB_MAX_CPUS];
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

static void slab_page_init(slab_page_t* page, size_t obj_size, uint16_t owner) {
    page->magic       = SLAB_MAGIC;
    page->obj_size    = (uint16_t)obj_size;
    page->next        = NULL;
    page->prev        = NULL;
    page->freelist    = NULL;
    page->owner_cpu   = owner;

    uintptr_t start = (uintptr_t)page + sizeof(slab_page_t);
    uintptr_t end   = (uintptr_t)page + SLAB_PAGE_SIZE;
    uint16_t  count = 0;

    for (uintptr_t p = start; p + obj_size <= end; p += obj_size) {
        slab_obj_t* obj = (slab_obj_t*)p;
        obj->next       = page->freelist;
        page->freelist  = obj;
        count++;
    }
    page->total_count = count;
    page->free_count  = count;
}

static slab_page_t* alloc_raw_page(size_t obj_size, uint16_t owner) {
    buddy_allocator_t* buddy = slab_get_buddy();
    void* raw = buddy_alloc(buddy, SLAB_PAGE_SIZE);
    if (!raw) return NULL;
    slab_page_t* page = (slab_page_t*)raw;
    slab_page_init(page, obj_size, owner);
    return page;
}

static int cache_index_for_size(uint16_t obj_size) {
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        if ((uint16_t)obj_sizes[i] == obj_size)
            return i;
    }
    return -1;
}

void slab_init(void) {
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        caches[i].obj_size = (uint16_t)obj_sizes[i];
        caches[i].partial  = NULL;
        caches[i].full     = NULL;
        spinlock_init(&caches[i].lock);
    }
    for (int c = 0; c < SLAB_MAX_CPUS; c++)
        cpu_slabs[c].initialized = 0;
    LOG_INFO("slab: initialized %d caches\n", SLAB_SIZES_COUNT);
}

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

static void* cpu_cache_alloc(cpu_cache_t* cc, uint32_t cpu_id, int idx) {
    slab_page_t* page = cc->partial;

    if (!page) {
        slab_cache_t* gc = &caches[idx];
        spinlock_acquire(&gc->lock);
        page = gc->partial;
        if (page) {
            list_remove(&gc->partial, page);
            spinlock_release(&gc->lock);
            page->owner_cpu = (uint16_t)cpu_id;
            list_push(&cc->partial, page);
        } else {
            spinlock_release(&gc->lock);
            page = alloc_raw_page(cc->obj_size, (uint16_t)cpu_id);
            if (!page) return NULL;
            list_push(&cc->partial, page);
        }
    }

    slab_obj_t* obj = page->freelist;
    page->freelist  = obj->next;
    page->free_count--;

    if (page->free_count == 0) {
        list_remove(&cc->partial, page);
        list_push(&cc->full, page);
    }

    return (void*)obj;
}

static void cpu_cache_free(cpu_cache_t* cc, slab_page_t* page, void* ptr) {
    bool was_full = (page->free_count == 0);

    slab_obj_t* obj = (slab_obj_t*)ptr;
    obj->next       = page->freelist;
    page->freelist  = obj;
    page->free_count++;

    if (was_full) {
        list_remove(&cc->full, page);
        list_push(&cc->partial, page);
    }
}

static void global_cache_free(slab_cache_t* gc, slab_page_t* page, void* ptr) {
    spinlock_acquire(&gc->lock);

    bool was_full = (page->free_count == 0);

    slab_obj_t* obj = (slab_obj_t*)ptr;
    obj->next       = page->freelist;
    page->freelist  = obj;
    page->free_count++;

    if (was_full) {
        list_remove(&gc->full, page);
        list_push(&gc->partial, page);
    }

    spinlock_release(&gc->lock);
}

void* slab_alloc(size_t size) {
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        if (size > caches[i].obj_size) continue;

        uint32_t cpu_id = smp_current_cpu_id();
        if (cpu_id < SLAB_MAX_CPUS && cpu_slabs[cpu_id].initialized)
            return cpu_cache_alloc(&cpu_slabs[cpu_id].caches[i], cpu_id, i);

        slab_cache_t* gc = &caches[i];
        spinlock_acquire(&gc->lock);

        slab_page_t* page = gc->partial;
        if (!page) {
            page = alloc_raw_page(gc->obj_size, SLAB_CPU_NONE);
            if (!page) {
                spinlock_release(&gc->lock);
                return NULL;
            }
            list_push(&gc->partial, page);
        }

        slab_obj_t* obj = page->freelist;
        page->freelist  = obj->next;
        page->free_count--;

        if (page->free_count == 0) {
            list_remove(&gc->partial, page);
            list_push(&gc->full, page);
        }

        spinlock_release(&gc->lock);
        return (void*)obj;
    }
    return NULL;
}

void slab_free(void* ptr) {
    if (!ptr) return;

    uintptr_t page_addr = (uintptr_t)ptr & ~(uintptr_t)(SLAB_PAGE_SIZE - 1);
    slab_page_t* page   = (slab_page_t*)page_addr;

    if (page->magic != SLAB_MAGIC) {
        LOG_WARN("slab_free: bad magic at %p\n", page);
        return;
    }

    int idx = cache_index_for_size(page->obj_size);
    if (idx < 0) return;

    uint16_t owner = page->owner_cpu;
    uint32_t cpu_id = smp_current_cpu_id();

    if (owner != SLAB_CPU_NONE && owner == (uint16_t)cpu_id
        && cpu_id < SLAB_MAX_CPUS && cpu_slabs[cpu_id].initialized) {
        cpu_cache_free(&cpu_slabs[cpu_id].caches[idx], page, ptr);
        return;
    }

    if (owner != SLAB_CPU_NONE && owner < SLAB_MAX_CPUS && cpu_slabs[owner].initialized) {
        global_cache_free(&caches[idx], page, ptr);
        page->owner_cpu = SLAB_CPU_NONE;
        return;
    }

    global_cache_free(&caches[idx], page, ptr);
}

int slab_owns(void* ptr) {
    if (!ptr) return 0;
    uintptr_t page_addr = (uintptr_t)ptr & ~(uintptr_t)(SLAB_PAGE_SIZE - 1);
    slab_page_t* page   = (slab_page_t*)page_addr;
    return (page->magic == SLAB_MAGIC);
}

void* slab_alloc_cpu(uint32_t cpu_id, size_t size) {
    if (cpu_id >= SLAB_MAX_CPUS || !cpu_slabs[cpu_id].initialized)
        return slab_alloc(size);

    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        if (size > (size_t)cpu_slabs[cpu_id].caches[i].obj_size) continue;
        return cpu_cache_alloc(&cpu_slabs[cpu_id].caches[i], cpu_id, i);
    }
    return NULL;
}

void slab_free_cpu(void* ptr) {
    slab_free(ptr);
}
