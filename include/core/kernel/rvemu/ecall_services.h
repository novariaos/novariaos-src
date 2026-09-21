#ifndef RVEMU_ECALL_SERVICES_H
#define RVEMU_ECALL_SERVICES_H

#include <core/kernel/rvemu/ecall.h>

#define RV64_ECALL_EXIT 0
#define RV64_ECALL_TTY_WRITE 1
#define RV64_ECALL_SPAWN 2
#define RV64_ECALL_SLEEP 3
#define RV64_ECALL_OPEN 0x10
#define RV64_ECALL_CLOSE 0x11
#define RV64_ECALL_READ 0x12
#define RV64_ECALL_WRITE 0x13
#define RV64_ECALL_MKDIR 0x14

void ecall_services_install(ecall_registry_t *registry);

#endif