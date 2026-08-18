#pragma once
#include "process.hpp"
#include "../memory/heap.hpp"
#include "../memory/vmm.hpp"

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

    // removes any dead threads from the head of the queue
    // currently unused
    void clean_up() {
        if (head == nullptr)
            return;

        struct thread *temp;
        while (head != nullptr && head->status == DEAD) {
            temp = peek();
            pop();
            free(temp->stack_base);
            free(temp->kernel_stack);
            free(temp);
        }
    }

    // pops the first ready thread from a queue and returns it
    // removes any dead threads it finds while walking the queue
    struct thread *extract_ready_thread() {
        if (head == nullptr)
            return head;

        struct thread *temp = head;
        if (head->status == READY) {
            pop();
            return temp;
        }

        while (head != nullptr && head->status == DEAD) {
            temp = peek();
            pop();
            free(temp->stack_base);
            free(temp->kernel_stack);
            if (temp->parent->nr_of_threads == 0) {
                VMM::destroy_address_space(temp->parent->root_page_table);
                free(temp->parent);
            }
            free(temp);
        }

        while (temp->next != nullptr) {
            if (temp->next->status == READY) {
                struct thread *next_thread = temp->next;
                temp->next = next_thread->next;
                if (next_thread == tail)
                    tail = temp;
                return next_thread;
            }
            else if (temp->next->status == DEAD) {
                // jump to next thread
                struct thread *next_thread = temp->next;
                temp->next = next_thread->next;
                if (next_thread == tail)
                    tail = temp;
                // free mem
                free(next_thread->kernel_stack);
                free(next_thread->stack_base);
                if (next_thread->parent->nr_of_threads == 0) {
                    VMM::destroy_address_space(next_thread->parent->root_page_table);
                    free(next_thread->parent);
                }
                free(next_thread);
                // continue so that next iteration of the while checks the new temp->next
                // not continuing would jump over a node
                continue;
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

