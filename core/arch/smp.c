#include <core/arch/smp.h>
#include <core/arch/idt.h>
#include <core/arch/work_queue.h>
#include <core/kernel/mem.h>
#include <core/kernel/mem/slab.h>
#include <core/kernel/mem/cpu_pool.h>
#include <core/kernel/kstd.h>
#include <core/arch/io.h>
#include <log.h>
#include <limine.h>
#include <stddef.h>
#include <stdint.h>

cpu_info_t       cpus[MAX_CPUS];
volatile uint32_t cpu_count  = 0;
volatile uint32_t cpus_online = 0;

#define LAPIC_BASE      0xFEE00000ULL
#define LAPIC_ID        0x020
#define LAPIC_ICR_LO    0x300
#define LAPIC_ICR_HI    0x310

static inline uint32_t lapic_read(uint32_t reg) {
    volatile uint32_t* addr = (volatile uint32_t*)(LAPIC_BASE + reg);
    return *addr;
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    volatile uint32_t* addr = (volatile uint32_t*)(LAPIC_BASE + reg);
    *addr = val;
}

uint32_t smp_current_cpu_id(void) {
    uint32_t lapic_id = lapic_read(LAPIC_ID) >> 24;
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].lapic_id == lapic_id)
            return cpus[i].cpu_id;
    }
    return 0;
}

void smp_send_ipi(uint32_t lapic_id, uint8_t vector) {
    lapic_write(LAPIC_ICR_HI, lapic_id << 24);
    lapic_write(LAPIC_ICR_LO, (uint32_t)vector | (1 << 14));
}

void smp_halt_all(void) {
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].cpu_id == smp_current_cpu_id()) continue;
        if (cpus[i].state == CPU_STATE_ONLINE)
            smp_send_ipi(cpus[i].lapic_id, 0xFF);
    }
}

static void ap_entry(struct limine_mp_info* info) {
    idt_load();

    cpu_info_t* cpu = (cpu_info_t*)info->extra_argument;
    cpu->state      = CPU_STATE_ONLINE;
    cpu->ready      = 1;

    slab_cpu_init(cpu->cpu_id);
    cpu_pool_init(cpu->cpu_id);

    __atomic_fetch_add(&cpus_online, 1, __ATOMIC_SEQ_CST);

    LOG_INFO("smp: AP %u online (lapic_id=%u)\n", cpu->cpu_id, cpu->lapic_id);

    while (1) {
        wq_run(cpu->cpu_id);
        __asm__ volatile("pause");
    }
}

void smp_init(struct limine_mp_response* mp) {
    if (!mp) {
        LOG_WARN("smp: no SMP response from bootloader\n");
        cpu_count  = 1;
        cpus_online = 1;
        cpus[0].cpu_id   = 0;
        cpus[0].lapic_id = 0;
        cpus[0].state    = CPU_STATE_ONLINE;
        cpus[0].ready    = 1;
        return;
    }

    cpu_count = (uint32_t)mp->cpu_count;
    if (cpu_count > MAX_CPUS) cpu_count = MAX_CPUS;

    LOG_INFO("smp: %u CPU(s) found\n", cpu_count);

    for (uint32_t i = 0; i < cpu_count; i++) {
        struct limine_mp_info* info = mp->cpus[i];
        cpus[i].cpu_id   = i;
        cpus[i].lapic_id = info->lapic_id;
        cpus[i].ready    = 0;

        if (info->lapic_id == mp->bsp_lapic_id) {
            cpus[i].state  = CPU_STATE_ONLINE;
            cpus[i].ready  = 1;
            cpus[i].stack  = NULL;
            __atomic_fetch_add(&cpus_online, 1, __ATOMIC_SEQ_CST);
            LOG_INFO("smp: BSP cpu%u online (lapic_id=%u)\n", i, info->lapic_id);
            continue;
        }

        void* stack = kmalloc(CPU_STACK_SIZE);
        if (!stack) {
            LOG_WARN("smp: failed to allocate stack for cpu%u\n", i);
            cpus[i].state = CPU_STATE_HALTED;
            continue;
        }
        cpus[i].stack = stack;
        cpus[i].state = CPU_STATE_STARTING;

        info->extra_argument = (uint64_t)&cpus[i];
        info->goto_address   = ap_entry;
    }

    uint32_t timeout = 100000000;
    while (cpus_online < cpu_count && timeout-- > 0)
        __asm__ volatile("pause");

    if (cpus_online < cpu_count)
        LOG_WARN("smp: only %u/%u CPUs came online\n", cpus_online, cpu_count);
    else
        LOG_INFO("smp: all %u CPUs online\n", cpus_online);
}

uint32_t smp_cpu_count(void) {
    return cpu_count;
}
