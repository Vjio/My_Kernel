#include "scheduler_queue.hpp"
#include "scheduler.hpp"
#include "process.hpp"
#include "../memory/heap.hpp"
#include "../memory/vmm.hpp"

void SchedulerQueue::promote_starving(Scheduler *scheduler, uint64_t interrupt_nr) {
    struct thread *temp = head;
    while (temp != nullptr && temp->is_starving(interrupt_nr)) {
        // update process
        temp->ready_time = interrupt_nr;
        temp->current_level++;
        // insert in next queue
        scheduler->insert_thread(temp);

        pop();
        temp = head;
    }    
}

bool SchedulerQueue::empty() {
    return head == nullptr;
} 

struct thread *SchedulerQueue::peek() {
    return head;
}

void SchedulerQueue::pop() {
    if (head == nullptr)
        return;
    struct thread *temp = head;
    head = head->next;
    temp->next = nullptr;
    if (!head)
        tail = nullptr;
}

void SchedulerQueue::push(struct thread *new_task) {
    if (tail)
        tail->next = new_task;
    else
        head = new_task;
    tail = new_task;
    new_task->next = nullptr;
}

void SchedulerQueue::clean_up() {
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

struct thread *SchedulerQueue::extract_ready_thread() {
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

    if (head == nullptr)
        return nullptr;

    temp = head;
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
