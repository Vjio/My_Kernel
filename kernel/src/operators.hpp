#pragma once
#include <cstddef>
#include "memory/heap.hpp"
#include "stdio.hpp"

void* operator new(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        // this shouldn't happen
        printf("new object creation failed!\n");
    }
    return ptr;
}

void* operator new[](size_t size) {
    return operator new(size);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    free(ptr);
}
