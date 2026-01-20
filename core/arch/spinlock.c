#include <core/arch/spinlock.h>

void spinlock_init(spinlock_t* lock) {
    lock->lock = 0;
}

void spinlock_acquire(spinlock_t* lock) {
    while (1) {
        // Test and set operation using x86 atomic exchange
        uint32_t expected = 0;
        uint32_t new_value = 1;

        __asm__ volatile (
            "lock cmpxchgl %2, %1"
            : "+a" (expected), "+m" (lock->lock)
            : "r" (new_value)
            : "cc", "memory"
        );

        if (expected == 0) {
            // Successfully acquired the lock
            break;
        }

        // Spin while waiting
        __asm__ volatile ("pause" ::: "memory");
    }
}

void spinlock_release(spinlock_t* lock) {
    __asm__ volatile (
        "movl $0, %0"
        : "=m" (lock->lock)
        : "m" (lock->lock)
        : "memory"
    );
}

bool spinlock_try_acquire(spinlock_t* lock) {
    uint32_t expected = 0;
    uint32_t new_value = 1;

    __asm__ volatile (
        "lock cmpxchgl %2, %1"
        : "+a" (expected), "+m" (lock->lock)
        : "r" (new_value)
        : "cc", "memory"
    );

    return expected == 0;
}
