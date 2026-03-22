// SPDX-License-Identifier: GPL-3.0-only

#include <core/drivers/nvme.h>
#include <core/fs/block.h>
#include <core/kernel/mem/allocator.h>
#include <log.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_CLASS_NVME  0x010802

#define VIRT_TO_PHYS(addr) ((uint64_t)(addr) - get_hhdm_offset())

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                    (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static uint64_t nvme_find_bar(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_read(0, slot, 0, 0x00);
        if (id == 0xFFFFFFFF) continue;

        uint32_t class_rev = pci_read(0, slot, 0, 0x08);
        uint32_t class_code = class_rev >> 8;

        if (class_code == PCI_CLASS_NVME) {
            LOG_DEBUG("NVMe: found at PCI 00:%02x.0 (class=0x%x)\n", slot, class_code);

            uint32_t bar0_lo = pci_read(0, slot, 0, 0x10);
            uint32_t bar0_hi = pci_read(0, slot, 0, 0x14);

            uint64_t bar = ((uint64_t)bar0_hi << 32) | (bar0_lo & ~0xFu);
            return bar;
        }
    }
    return 0;
}

static volatile nvme_controller_regs_t* nvme_regs = NULL;
static nvme_command_t* admin_sq = NULL;
static nvme_completion_t* admin_cq = NULL;
static nvme_command_t* io_sq = NULL;
static nvme_completion_t* io_cq = NULL;

static uint16_t admin_sq_tail = 0;
static uint16_t admin_cq_head = 0;
static uint16_t io_sq_tail = 0;
static uint16_t io_cq_head = 0;
static uint16_t command_id = 0;
static uint8_t admin_cq_phase = 1;
static uint8_t io_cq_phase = 1;

static uint32_t nsid = 1;
static uint64_t block_count = 0;

static inline void mmio_write32(volatile uint32_t* addr, uint32_t value) {
    *addr = value;
    __asm__ volatile("" ::: "memory");
}

static inline uint32_t mmio_read32(volatile uint32_t* addr) {
    uint32_t value = *addr;
    __asm__ volatile("" ::: "memory");
    return value;
}

static inline void mmio_write64(volatile uint64_t* addr, uint64_t value) {
    *addr = value;
    __asm__ volatile("" ::: "memory");
}

static inline uint64_t mmio_read64(volatile uint64_t* addr) {
    uint64_t value = *addr;
    __asm__ volatile("" ::: "memory");
    return value;
}

static void nvme_write_doorbell(uint32_t queue_id, uint32_t value, bool is_sq) {
    uint64_t cap = mmio_read64(&nvme_regs->cap);
    uint32_t dstrd = ((cap & NVME_CAP_DSTRD_MASK) >> NVME_CAP_DSTRD_SHIFT);
    uint32_t doorbell_offset = queue_id * 2 * (1 << dstrd);

    if (!is_sq) {
        doorbell_offset += (1 << dstrd);
    }

    mmio_write32(&nvme_regs->doorbells[doorbell_offset], value);
}

static int nvme_wait_ready(bool ready_state, uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms * 100; i++) {
        uint32_t csts = mmio_read32(&nvme_regs->csts);
        bool is_ready = (csts & NVME_CSTS_RDY) != 0;

        if (is_ready == ready_state) {
            return 0;
        }

        for (volatile int j = 0; j < 1000; j++);
    }

    return -1;
}

static int nvme_submit_admin_command(nvme_command_t* cmd) {
    uint16_t slot = admin_sq_tail;
    memcpy(&admin_sq[slot], cmd, sizeof(nvme_command_t));

    admin_sq_tail = (admin_sq_tail + 1) % NVME_ADMIN_QUEUE_SIZE;
    nvme_write_doorbell(0, admin_sq_tail, true);

    for (int timeout = 0; timeout < 10000; timeout++) {
        nvme_completion_t* cqe = &admin_cq[admin_cq_head];
        uint8_t phase = (cqe->status >> 0) & 1;

        if (phase == admin_cq_phase) {
            uint16_t status_code = (cqe->status >> 1) & 0x7FFF;
            admin_cq_head = (admin_cq_head + 1) % NVME_ADMIN_QUEUE_SIZE;

            if (admin_cq_head == 0) {
                admin_cq_phase = !admin_cq_phase;
            }

            nvme_write_doorbell(0, admin_cq_head, false);

            if (status_code != 0) {
                LOG_ERROR("NVMe admin command failed with status: 0x%x\n", status_code);
                return -1;
            }

            return 0;
        }

        for (volatile int i = 0; i < 1000; i++);
    }

    LOG_ERROR("NVMe admin command timeout\n");
    return -1;
}

static int nvme_identify_namespace(uint32_t ns_id, void* data) {
    nvme_command_t cmd = {0};
    cmd.cdw0 = NVME_ADMIN_IDENTIFY;
    cmd.nsid = ns_id;
    cmd.prp1 = VIRT_TO_PHYS(data);
    cmd.cdw10 = NVME_IDENTIFY_CNS_NAMESPACE;

    return nvme_submit_admin_command(&cmd);
}

static int nvme_create_io_completion_queue(uint16_t qid, uint16_t size, void* buffer) {
    nvme_command_t cmd = {0};
    cmd.cdw0 = NVME_ADMIN_CREATE_CQ;
    cmd.prp1 = VIRT_TO_PHYS(buffer);
    cmd.cdw10 = ((uint32_t)(size - 1) << 16) | qid;
    cmd.cdw11 = NVME_QUEUE_PHYS_CONTIG | NVME_CQ_IRQ_ENABLED;

    return nvme_submit_admin_command(&cmd);
}

static int nvme_create_io_submission_queue(uint16_t qid, uint16_t cqid, uint16_t size, void* buffer) {
    nvme_command_t cmd = {0};
    cmd.cdw0 = NVME_ADMIN_CREATE_SQ;
    cmd.prp1 = VIRT_TO_PHYS(buffer);
    cmd.cdw10 = ((uint32_t)(size - 1) << 16) | qid;
    cmd.cdw11 = (cqid << 16) | NVME_QUEUE_PHYS_CONTIG;

    return nvme_submit_admin_command(&cmd);
}

static int nvme_reset_controller(void) {
    LOG_DEBUG("NVMe: Resetting controller...\n");

    uint32_t cc = mmio_read32(&nvme_regs->cc);
    cc &= ~NVME_CC_ENABLE;
    mmio_write32(&nvme_regs->cc, cc);

    if (nvme_wait_ready(false, 5000) < 0) {
        LOG_ERROR("NVMe: Controller disable timeout\n");
        return -1;
    }

    LOG_DEBUG("NVMe: Controller disabled\n");
    return 0;
}

static int nvme_enable_controller(void) {
    uint64_t cap = mmio_read64(&nvme_regs->cap);
    uint32_t page_shift = 12;

    uint32_t cc = 0;
    cc |= NVME_CC_ENABLE;
    cc |= NVME_CC_CSS_NVM;
    cc |= ((page_shift - 12) << NVME_CC_MPS_SHIFT);
    cc |= NVME_CC_AMS_RR;
    cc |= NVME_CC_SHN_NONE;
    cc |= NVME_CC_IOSQES;
    cc |= NVME_CC_IOCQES;

    mmio_write32(&nvme_regs->cc, cc);

    if (nvme_wait_ready(true, 5000) < 0) {
        LOG_ERROR("NVMe: Controller enable timeout\n");
        return -1;
    }

    LOG_DEBUG("NVMe: Controller enabled\n");
    return 0;
}

static void* alloc_page_aligned(size_t size) {
    void* raw = kmalloc(size + 4096);
    if (!raw) return NULL;
    return (void*)(((uintptr_t)raw + 4095) & ~(uintptr_t)4095);
}

static int nvme_setup_admin_queues(void) {
    admin_sq = alloc_page_aligned(NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_command_t));
    admin_cq = alloc_page_aligned(NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_completion_t));

    if (!admin_sq || !admin_cq) {
        LOG_ERROR("NVMe: Failed to allocate admin queues\n");
        return -1;
    }

    memset(admin_sq, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_command_t));
    memset(admin_cq, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_completion_t));

    uint32_t aqa = ((NVME_ADMIN_QUEUE_SIZE - 1) << 16) | (NVME_ADMIN_QUEUE_SIZE - 1);
    mmio_write32(&nvme_regs->aqa, aqa);
    mmio_write64(&nvme_regs->asq, VIRT_TO_PHYS(admin_sq));
    mmio_write64(&nvme_regs->acq, VIRT_TO_PHYS(admin_cq));

    LOG_DEBUG("NVMe: Admin queues configured\n");
    return 0;
}

static int nvme_setup_io_queues(void) {
    io_sq = alloc_page_aligned(NVME_IO_QUEUE_SIZE * sizeof(nvme_command_t));
    io_cq = alloc_page_aligned(NVME_IO_QUEUE_SIZE * sizeof(nvme_completion_t));

    if (!io_sq || !io_cq) {
        LOG_ERROR("NVMe: Failed to allocate I/O queues\n");
        return -1;
    }

    memset(io_sq, 0, NVME_IO_QUEUE_SIZE * sizeof(nvme_command_t));
    memset(io_cq, 0, NVME_IO_QUEUE_SIZE * sizeof(nvme_completion_t));

    if (nvme_create_io_completion_queue(1, NVME_IO_QUEUE_SIZE, io_cq) < 0) {
        LOG_ERROR("NVMe: Failed to create I/O completion queue\n");
        return -1;
    }

    if (nvme_create_io_submission_queue(1, 1, NVME_IO_QUEUE_SIZE, io_sq) < 0) {
        LOG_ERROR("NVMe: Failed to create I/O submission queue\n");
        return -1;
    }

    LOG_DEBUG("NVMe: I/O queues created\n");
    return 0;
}

static int nvme_read_blocks(struct block_device* dev, uint64_t lba, size_t count, void* buf) {
    if (lba + count > block_count) {
        return -1;
    }

    while (count > 0) {
        size_t blocks_this_cmd = (count > 256) ? 256 : count;

        nvme_command_t cmd = {0};
        cmd.cdw0 = NVME_CMD_READ | ((uint32_t)(command_id++) << 16);
        cmd.nsid = nsid;
        cmd.prp1 = VIRT_TO_PHYS(buf);
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = (blocks_this_cmd - 1) & 0xFFFF;

        uint16_t slot = io_sq_tail;
        memcpy(&io_sq[slot], &cmd, sizeof(nvme_command_t));

        io_sq_tail = (io_sq_tail + 1) % NVME_IO_QUEUE_SIZE;
        nvme_write_doorbell(1, io_sq_tail, true);

        int timeout = 0;
        while (timeout < 10000) {
            nvme_completion_t* cqe = &io_cq[io_cq_head];
            uint8_t phase = (cqe->status >> 0) & 1;

            if (phase == io_cq_phase) {
                uint16_t status_code = (cqe->status >> 1) & 0x7FFF;
                io_cq_head = (io_cq_head + 1) % NVME_IO_QUEUE_SIZE;

                if (io_cq_head == 0) {
                    io_cq_phase = !io_cq_phase;
                }

                nvme_write_doorbell(1, io_cq_head, false);

                if (status_code != 0) {
                    LOG_ERROR("NVMe: Read command failed with status 0x%x\n", status_code);
                    return -1;
                }

                break;
            }

            for (volatile int i = 0; i < 1000; i++);
            timeout++;
        }

        if (timeout >= 10000) {
            LOG_ERROR("NVMe: Read command timeout\n");
            return -1;
        }

        buf = (void*)((uint64_t)buf + blocks_this_cmd * 512);
        lba += blocks_this_cmd;
        count -= blocks_this_cmd;
    }

    return 0;
}

static int nvme_write_blocks(struct block_device* dev, uint64_t lba, size_t count, const void* buf) {
    if (lba + count > block_count) {
        return -1;
    }

    while (count > 0) {
        size_t blocks_this_cmd = (count > 256) ? 256 : count;

        nvme_command_t cmd = {0};
        cmd.cdw0 = NVME_CMD_WRITE | ((uint32_t)(command_id++) << 16);
        cmd.nsid = nsid;
        cmd.prp1 = VIRT_TO_PHYS(buf);
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = (blocks_this_cmd - 1) & 0xFFFF;

        uint16_t slot = io_sq_tail;
        memcpy(&io_sq[slot], &cmd, sizeof(nvme_command_t));

        io_sq_tail = (io_sq_tail + 1) % NVME_IO_QUEUE_SIZE;
        nvme_write_doorbell(1, io_sq_tail, true);

        int timeout = 0;
        while (timeout < 10000) {
            nvme_completion_t* cqe = &io_cq[io_cq_head];
            uint8_t phase = (cqe->status >> 0) & 1;

            if (phase == io_cq_phase) {
                uint16_t status_code = (cqe->status >> 1) & 0x7FFF;
                io_cq_head = (io_cq_head + 1) % NVME_IO_QUEUE_SIZE;

                if (io_cq_head == 0) {
                    io_cq_phase = !io_cq_phase;
                }

                nvme_write_doorbell(1, io_cq_head, false);

                if (status_code != 0) {
                    LOG_ERROR("NVMe: Write command failed with status 0x%x\n", status_code);
                    return -1;
                }

                break;
            }

            for (volatile int i = 0; i < 1000; i++);
            timeout++;
        }

        if (timeout >= 10000) {
            LOG_ERROR("NVMe: Write command timeout\n");
            return -1;
        }

        buf = (const void*)((uint64_t)buf + blocks_this_cmd * 512);
        lba += blocks_this_cmd;
        count -= blocks_this_cmd;
    }

    return 0;
}

void nvme_init(void) {
    LOG_DEBUG("NVMe: Initializing driver...\n");

    uint64_t bar = nvme_find_bar();
    if (bar == 0) {
        LOG_DEBUG("NVMe: No controller found via PCI scan\n");
        return;
    }

    LOG_DEBUG("NVMe: BAR at 0x%llx\n", bar);
    nvme_regs = (volatile nvme_controller_regs_t*)bar;

    uint64_t cap = mmio_read64(&nvme_regs->cap);
    if (cap == 0xFFFFFFFFFFFFFFFFULL || cap == 0) {
        LOG_DEBUG("NVMe: Invalid CAP at BAR 0x%llx\n", bar);
        return;
    }

    LOG_DEBUG("NVMe: Controller found, CAP=0x%llx\n", cap);

    if (nvme_reset_controller() < 0) {
        return;
    }

    if (nvme_setup_admin_queues() < 0) {
        return;
    }

    if (nvme_enable_controller() < 0) {
        return;
    }

    uint8_t* identify_data = alloc_page_aligned(4096);
    if (!identify_data) {
        LOG_ERROR("NVMe: Failed to allocate identify buffer\n");
        return;
    }

    memset(identify_data, 0, 4096);

    if (nvme_identify_namespace(nsid, identify_data) < 0) {
        LOG_ERROR("NVMe: Failed to identify namespace\n");
        return;
    }

    block_count = *(uint64_t*)identify_data;
    LOG_DEBUG("NVMe: Namespace size: %llu blocks\n", block_count);

    if (nvme_setup_io_queues() < 0) {
        return;
    }

    block_device_ops_t ops = {
        .read_blocks = nvme_read_blocks,
        .write_blocks = nvme_write_blocks,
    };

    register_block_device("nvme0n1", 512, block_count, &ops, NULL);

    LOG_DEBUG("NVMe: Initialization complete\n");
}
