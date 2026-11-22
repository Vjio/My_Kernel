#include "keyboard_driver.hpp"
#include "scheduling/scheduler.hpp"

bool KeyboardDriver::keyboard_try_get_event(key_event& out) {
    return ring_buffer_dequeue(rb, &out, 1) == 1;
}

void KeyboardDriver::keyboard_get_event(key_event& out) {
    struct thread *thread = Scheduler::get_current_scheduler()->get_running_thread();

    for (;;) {
        if (ring_buffer_dequeue(rb, &out, 1) == 1) {
            return;
        }

        uint64_t flags = spin_lock_irq_save(&rb->spinlock);

        // someone is already waiting on the keyboard
        if (rb->waiting_thread != nullptr && rb->waiting_thread != thread) {
            // TODO: add support for multiple threads to wait on the keyboard
            spin_lock_irq_restore(&rb->spinlock, flags);
            out = {KEY_NONE, false};
            return;
        }

        // an event may have arrived between the dequeue above and getting the lock
        if (!ring_buffer_empty(rb)) {
            // go back and dequeue it
            spin_lock_irq_restore(&rb->spinlock, flags);
            continue;
        }

        // buffer is empty. put the thread into a waiting state until a key event
        // appears
        rb->waiting_thread = thread;
        thread->status = WAITING;
        spin_lock_irq_restore(&rb->spinlock, flags);

        thread->wait_until_taken_out_of_waiting();
    }
}
