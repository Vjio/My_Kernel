#pragma once
#include "process.hpp"
#include "../memory/heap.hpp"

class Scheduler;

class SchedulerQueue {
    public:
    SchedulerQueue() {}

    // returns true if queue is empty
    bool empty() {
        return head == nullptr;
    } 

    // returns first element in queue
    struct thread *peek() {
        return head;
    }

    // removes first element from queue
    void pop() {
        if (head == nullptr)
            return;
        head = head->next;
        if (!head)
            tail = nullptr;
    }

    // adds an element to the end of the queue
    void push(struct thread *new_task) {
        if (tail)
            tail->next = new_task;
        else
            head = new_task;
        tail = new_task;
    }

    // removes any dead threads from queue
    void clean_up() {
        if (head == nullptr)
            return;
        
        struct thread *temp;
        while (head != nullptr && head->status == DEAD) {
            temp = peek();
            free(temp->stack_base);
            free(temp);
            pop();
        }
    }

    struct thread *extract_ready_thread() {
        if (head == nullptr)
            return head;

        struct thread *temp = head;
        if (head->status == READY) {
            pop();
            return temp;
        }
        
        while (temp->next != nullptr) {
            if (temp->next->status == READY) {
                struct thread *next_thread = temp->next;
                temp->next = next_thread->next;
                if (next_thread == tail)
                    tail = temp;
                return next_thread;
            }
            temp = temp->next;
        }

        return nullptr;
    }

    // DO NOT call this function outside of scheduler
    // promotes starving processes to the next queue
    void promote_starving(Scheduler *scheduler, uint64_t interrupt_nr);

    private:
    struct thread *head = nullptr;
    struct thread *tail = nullptr;
};

