#pragma once
#include <cstdint>
#include <cstddef>

class Scheduler;
struct thread;

class SchedulerQueue {
    public:
    SchedulerQueue() {}

    // returns true if queue is empty
    bool empty();
    // returns first element in queue
    struct thread *peek();
    // removes first element from queue
    void pop();
    // adds an element to the end of the queue
    void push(struct thread *new_task);
    // removes any dead threads from the head of the queue
    // currently unused
    void clean_up();
    // pops the first ready thread from a queue and returns it
    // removes any dead threads it finds while walking the queue
    struct thread *extract_ready_thread();
    // DO NOT call this function outside of scheduler
    // promotes starving processes to the next queue
    void promote_starving(Scheduler *scheduler, uint64_t interrupt_nr);

    private:
    struct thread *head = nullptr;
    struct thread *tail = nullptr;
};
