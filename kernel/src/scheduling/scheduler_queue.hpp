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
    struct process *peek() {
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
    void push(struct process *new_task) {
        if (tail)
            tail->next = new_task;
        else
            head = new_task;
        tail = new_task;
    }

    // removes any dead process from queue
    void clean_up() {
        if (head == nullptr)
            return;
        
        struct process *temp;
        while (head != nullptr && head->status == DEAD) {
            temp = peek();
            free(temp);
            pop();
        }
    }

    struct process *extract_ready_process() {
        if (head == nullptr)
            return head;

        struct process *temp = head;
        if (head->status == READY) {
            pop();
            return temp;
        }
        
        while (temp->next != nullptr) {
            if (temp->next->status == READY) {
                struct process *next_process = temp->next;
                temp->next = next_process->next;
                if (next_process == tail)
                    tail = temp;
                return next_process;
            }
            temp = temp->next;
        }

        return nullptr;
    }

    // DO NOT call this function outside of scheduler
    // promotes starving processes to the next queue
    void promote_starving(Scheduler *scheduler, uint64_t interrupt_nr);

    private:
    struct process *head = nullptr;
    struct process *tail = nullptr;
};

