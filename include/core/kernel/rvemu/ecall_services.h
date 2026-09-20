#ifndef RVEMU_ECALL_SERVICES_H
#define RVEMU_ECALL_SERVICES_H

#include <core/kernel/rvemu/ecall.h>

#define RV64_ECALL_EXIT 0
#define RV64_ECALL_TTY_WRITE 1

void ecall_services_install(ecall_registry_t *registry);

#endif