// SPDX-License-Identifier: GPL-3.0-only

#include <core/kernel/nvm/nvm.h>
#include <core/kernel/nvm/caps.h>
#include <core/drivers/serial.h>

bool caps_has_capability(nvm_process_t* proc, int16_t cap) {
    if (!proc) return false;
    
    for (int i = 0; i < proc->caps_count; i++) {
        if (proc->capabilities[i] == CAP_ALL) return true;
    }
    
    // Check specific capability
    for (int i = 0; i < proc->caps_count; i++) {
        if (proc->capabilities[i] == cap) return true;
    }
    
    return false;
}

bool caps_add_capability(nvm_process_t* proc, int16_t cap) {
    if (!proc || proc->caps_count >= MAX_CAPS) return false;
    
    // We check whether such a capability already exists.
    if (caps_has_capability(proc, cap)) return true;
    
    proc->capabilities[proc->caps_count++] = cap;
    return true;
}

bool caps_remove_capability(nvm_process_t* proc, int16_t cap) {
    if (!proc) return false;
    
    for (int i = 0; i < proc->caps_count; i++) {
        if (proc->capabilities[i] == cap) {
            // move the remaining elements
            for (int j = i; j < proc->caps_count - 1; j++) {
                proc->capabilities[j] = proc->capabilities[j + 1];
            }
            proc->caps_count--;
            return true;
        }
    }
    return false;
}

void caps_clear_all(nvm_process_t* proc) {
    if (proc) {
        proc->caps_count = 0;
    }
}

bool caps_copy(nvm_process_t* dest, nvm_process_t* src) {
    if (!dest || !src) return false;
    
    dest->caps_count = src->caps_count;
    for (int i = 0; i < src->caps_count; i++) {
        dest->capabilities[i] = src->capabilities[i];
    }
    return true;
}