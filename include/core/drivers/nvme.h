// SPDX-License-Identifier: GPL-3.0-only

#ifndef NVME_H
#define NVME_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

#include <stdint.h>
#include <stddef.h>

#define NVME_ADMIN_QUEUE_SIZE 64
#define NVME_IO_QUEUE_SIZE    256
#define NVME_PAGE_SIZE        4096

#define NVME_CC_ENABLE        (1 << 0)
#define NVME_CC_CSS_NVM       (0 << 4)
#define NVME_CC_MPS_SHIFT     7
#define NVME_CC_AMS_RR        (0 << 11)
#define NVME_CC_SHN_NONE      (0 << 14)
#define NVME_CC_IOSQES        (6 << 16)
#define NVME_CC_IOCQES        (4 << 20)

#define NVME_CSTS_RDY         (1 << 0)
#define NVME_CSTS_CFS         (1 << 1)
#define NVME_CSTS_SHST_MASK   (3 << 2)
#define NVME_CSTS_SHST_NORMAL (0 << 2)
#define NVME_CSTS_SHST_OCCUR  (1 << 2)
#define NVME_CSTS_SHST_CMPLT  (2 << 2)

#define NVME_CAP_MQES_MASK    0xFFFF
#define NVME_CAP_DSTRD_SHIFT  32
#define NVME_CAP_DSTRD_MASK   ((uint64_t)0xF << NVME_CAP_DSTRD_SHIFT)
#define NVME_CAP_MPSMIN_SHIFT 48
#define NVME_CAP_MPSMIN_MASK  (0xF << NVME_CAP_MPSMIN_SHIFT)

#define NVME_ADMIN_IDENTIFY   0x06
#define NVME_ADMIN_CREATE_SQ  0x01
#define NVME_ADMIN_CREATE_CQ  0x05

#define NVME_CMD_READ         0x02
#define NVME_CMD_WRITE        0x01

#define NVME_QUEUE_PHYS_CONTIG (1 << 0)
#define NVME_CQ_IRQ_ENABLED    (1 << 1)

#define NVME_IDENTIFY_CNS_NAMESPACE  0
#define NVME_IDENTIFY_CNS_CONTROLLER 1
#define NVME_IDENTIFY_CNS_NS_LIST    2

#define NVME_MAX_NAMESPACES 16

typedef struct {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t reserved0;
    uint32_t csts;
    uint32_t nssr;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
    uint32_t cmbloc;
    uint32_t cmbsz;
    uint32_t reserved1[0x3F0];
    uint32_t doorbells[];
} __attribute__((packed)) nvme_controller_regs_t;

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_command_t;

typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} __attribute__((packed)) nvme_completion_t;

typedef struct {
    uint64_t lbaf_support;
    uint32_t namespace_size;
    uint32_t namespace_capacity;
    uint8_t  reserved[4];
    uint8_t  lba_format_index;
    uint8_t  metadata_capabilities;
    uint8_t  data_protection;
    uint8_t  reserved2[99];
} __attribute__((packed)) nvme_namespace_data_t;

typedef struct {
    uint16_t vid;
    uint16_t ssvid;
    char     serial[20];
    char     model[40];
    char     firmware[8];
    uint8_t  rab;
    uint8_t  ieee[3];
    uint8_t  reserved[256];
} __attribute__((packed)) nvme_identify_controller_t;

void nvme_init(void);

#endif
