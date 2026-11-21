#pragma once

struct spinlock {
    bool locked;
};

inline void acquire(struct spinlock *lock) {
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED))
            __builtin_ia32_pause();
    }
}

inline void release(struct spinlock *lock) {
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}
