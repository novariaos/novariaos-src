#ifndef ARCH_PAUSE_H
#define ARCH_PAUSE_H

#define cpu_relax() asm volatile("pause" ::: "memory")

#endif // PAUSE_H
