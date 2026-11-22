#pragma once
#include <cstdint>
#include "spinlock.h"

#define RFLAGS_IF   (1u << 9)

// disables interrupts then takes the lock
inline uint64_t spin_lock_irq_save(struct spinlock *lock) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    acquire(lock);
    return flags;
}

// enables interrupts only if they were enabled before spin_lock_irq_save()
inline void spin_lock_irq_restore(struct spinlock *lock, uint64_t flags) {
    release(lock);
    if (flags & RFLAGS_IF)
        __asm__ volatile("sti" ::: "memory");
}
