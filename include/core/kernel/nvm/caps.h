#ifndef CAPS_H
#define CAPS_H

#include <stdint.h>
#include <stdbool.h>
#include <core/kernel/nvm/nvm.h>

#define CAPS_NONE             0x0000
#define CAP_FS_READ           0x0001
#define CAP_FS_WRITE          0x0002
#define CAP_FS_CREATE         0x0003
#define CAP_FS_DELETE         0x0004
#define CAP_MEM_MGMT          0x0005
#define CAP_DRV_ACCESS        0x0006
#define CAP_PROC_MGMT         0x0007
#define CAP_CAPS_MGMT         0x0008
#define CAP_DRV_GROUP_STORAGE 0x0100
#define CAP_DRV_GROUP_VIDEO   0x0200
#define CAP_DRV_GROUP_AUDIO   0x0300
#define CAP_DRV_GROUP_NETWORK 0x0400
#define CAP_ALL               0xFFFF

bool caps_has_capability(nvm_process_t* proc, int16_t cap);
bool caps_add_capability(nvm_process_t* proc, int16_t cap);
bool caps_remove_capability(nvm_process_t* proc, int16_t cap);
void caps_clear_all(nvm_process_t* proc);
bool caps_copy(nvm_process_t* dest, nvm_process_t* src);

#endif
