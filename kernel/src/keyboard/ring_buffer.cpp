#include "ring_buffer.hpp"
#include "../memory/heap.hpp"
#include "memory.hpp"

// NOTE: this ring buffer is currently designed to only work with 1 producer and
// 1 consumer, with no locks
// in order to avoid race conditions, the producer will drop new key events
// if the buffer is full

struct ring_buffer *ring_buffer_init(size_t cap) {
    if (cap == 0)
        cap = RB_DEFAULT_CAP;

    // cap * sizeof must not overflow
    if (cap > SIZE_MAX / sizeof(struct key_event))
        return nullptr;

    struct ring_buffer *rb = reinterpret_cast<struct ring_buffer *>(calloc(1, sizeof(struct ring_buffer)));
    if (rb == nullptr)
        return nullptr;

    rb->data = reinterpret_cast<struct key_event *>(malloc(cap * sizeof(struct key_event)));
    if (rb->data == nullptr) {
        free(rb);
        return nullptr;
    }

    rb->cap = cap;
    return rb;
}

size_t ring_buffer_enqueue(struct ring_buffer *rb, struct key_event *data, size_t nr) {
    if (rb == nullptr || rb->data == nullptr || data == nullptr)
        return 0;

    size_t free_space = rb->cap - rb->len.load(std::memory_order_acquire);
    if (nr > free_space)
        nr = free_space;

    for (size_t i = 0; i < nr; i++) {
        rb->data[rb->write_pos] = data[i];
        rb->write_pos++;
        if (rb->write_pos == rb->cap)
            rb->write_pos = 0;
    }
    rb->len.fetch_add(nr, std::memory_order_release);

    if (rb->waiting_thread != nullptr) {
        struct thread *thread = rb->waiting_thread;
        rb->waiting_thread = nullptr;
        thread->take_thread_out_of_waiting();
    }

    return nr;
}

size_t ring_buffer_dequeue(struct ring_buffer *rb, struct key_event *data, size_t nr) {
    if (rb == nullptr || rb->data == nullptr || data == nullptr)
        return 0;

    size_t available = rb->len.load(std::memory_order_acquire);
    size_t n = nr < available ? nr : available;

    for (size_t i = 0; i < n; i++) {
        data[i] = rb->data[rb->read_pos];
        rb->read_pos++;
        if (rb->read_pos == rb->cap)
            rb->read_pos = 0;
    }

    rb->len.fetch_sub(n, std::memory_order_release);
    return n;
}

void ring_buffer_destroy(struct ring_buffer *rb) {
    if (rb == nullptr)
        return;

    if (rb->data != nullptr)
        free(rb->data);
    free(rb);
}

bool ring_buffer_empty(struct ring_buffer *rb) {
    return rb == nullptr || rb->len.load(std::memory_order_acquire) == 0;
}