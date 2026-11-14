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

        // someone is already waiting on the keyboard
        if (rb->waiting_thread != nullptr && rb->waiting_thread != thread) {
            // TODO: add support for multiple threads to wait on the keyboard
            out = {KEY_NONE, false};
            return;
        }

        // buffer is empty. put the thread into a waiting state until a key event
        // appears
        rb->waiting_thread = thread;
        thread->status = WAITING;

        thread->wait_until_taken_out_of_waiting();
    }
}
