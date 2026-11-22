#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include "keyboard.hpp"
#include "scheduling/process.hpp"
#include "../lock/spinlock_irq.hpp"

#define RB_DEFAULT_CAP  64

struct ring_buffer {
    struct key_event *data;

    size_t read_pos;
    size_t write_pos;

    // current stored events
    std::atomic<size_t> len;
    // maximum events that can be stored
    size_t cap;

    struct thread *waiting_thread;
    struct spinlock spinlock;
};

// returns NULL if init fails
struct ring_buffer *ring_buffer_init(size_t cap);
// returns how much was written to the ring buffer
size_t ring_buffer_enqueue(struct ring_buffer *rb, struct key_event *data, size_t nr);
// returns how much was read from the ring buffer
size_t ring_buffer_dequeue(struct ring_buffer *rb, struct key_event *data, size_t nr);
void   ring_buffer_destroy(struct ring_buffer *rb);
bool   ring_buffer_empty(struct ring_buffer *rb);
