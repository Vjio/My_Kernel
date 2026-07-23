#include "scheduler_queue.hpp"
#include "scheduler.hpp"

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
