#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct spinlock {
    volatile uint32_t lock;
} spinlock_t;

void spinlock_init(spinlock_t* lock);
void spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);
bool spinlock_try_acquire(spinlock_t* lock);

#endif // SPINLOCK_H
