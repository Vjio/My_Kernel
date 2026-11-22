#include "tty.hpp"
#include "scheduling/process.hpp"
#include "memory/heap.hpp"
#include "stdio.hpp"
#include "../scheduling/scheduler.hpp"

static void echo_str(struct tty *tty, const char *s) {
    while (*s)
        tty->echo(*s++);
}

// moves the first len bytes of the line into the ready queue
static void commit_line(struct tty *tty, size_t len) {
    acquire(&tty->lock);

    // if nobody reads and the queue fills up, the line gets dropped
    if (TTY_READY_MAX - tty->ready_len >= len) {
        for (size_t i = 0; i < len; i++) {
            size_t pos = (tty->ready_head + tty->ready_len) % TTY_READY_MAX;
            tty->ready[pos] = tty->line[i];
            tty->ready_len++;
        }
    }

    struct thread *thread = tty->waiting_thread;
    tty->waiting_thread = nullptr;
    release(&tty->lock);

    if (thread != nullptr)
        thread->take_thread_out_of_waiting();
}

void tty_input_char(struct tty *tty, char c) {
    switch (c) {
        case '\n':
            // always fits: printable characters stop one byte before the end
            tty->line[tty->line_len++] = '\n';
            tty->echo('\n');
            commit_line(tty, tty->line_len);
            tty->line_len = 0;
            break;

        case 0x7F:      // backspace
        case 0x08:
            if (tty->line_len > 0) {
                tty->line_len--;
                echo_str(tty, "\b \b");
            }
            break;

        case 0x15:      // ctrl-u: erase the whole line
            while (tty->line_len > 0) {
                tty->line_len--;
                echo_str(tty, "\b \b");
            }
            break;

        case 0x04:      // ctrl-d
            if (tty->line_len == 0) {
                // end of file: the next read returns 0
                acquire(&tty->lock);
                tty->eof = true;
                struct thread *thread = tty->waiting_thread;
                tty->waiting_thread = nullptr;
                release(&tty->lock);
                
                if (thread)
                    thread->take_thread_out_of_waiting();
            } else {
                // hand over what was typed, without a newline
                commit_line(tty, tty->line_len);
                tty->line_len = 0;
            }
            break;

        case 0x03:      // ctrl-c
            echo_str(tty, "^C\n");
            tty->line_len = 0;
            break;

        default:        // printable ASCII
            if (c >= 0x20 && c < 0x7F && tty->line_len < TTY_LINE_MAX - 1) {
                tty->line[tty->line_len++] = c;
                tty->echo(c);
            }

            if (tty->line_len == TTY_LINE_MAX - 2) {
                tty->line[tty->line_len++] = '\n';
                tty->echo('\n');
                commit_line(tty, tty->line_len);
                tty->line_len = 0;
            }
            break;
    }
}

void tty_handle_event(struct tty *tty, const key_event &event) {
    keyboard_state_update(tty->keyboard_state, event);

    if (event.released)
        return;

    char c = keycode_to_char(event.keycode, tty->keyboard_state);
    if (c != 0)
        tty_input_char(tty, c);
}

static void tty_routine(void *arg) {
    struct tty *tty = static_cast<struct tty *>(arg);
    key_event event;

    for (;;) {
        tty->keyboard_device->keyboard_get_event(event);
        tty_handle_event(tty, event);
    }
}

long tty_read(struct tty *tty, char *buf, int n) {
    if(tty == nullptr || buf == nullptr)
        return -1;
    
    if (n == 0)
        return 0;

    size_t read_bytes = 0;
    acquire(&tty->lock);
    while (true) {
        if (tty->ready_len > 0) {
            while (read_bytes < n & tty->ready_len > 0) {
                char c = tty->ready[tty->ready_head];
                tty->ready_head = (tty->ready_head + 1) % TTY_READY_MAX;
                tty->ready_len--;

                buf[++read_bytes] = c;
                if (c == '\n')
                    break;
            }
            release(&tty->lock);
            return read_bytes;
        }

        if (tty->eof) {
            tty->eof = false;
            release(&tty->lock);
            return 0;
        }

        struct thread *thread = Scheduler::get_current_scheduler()->get_running_thread();
        if (tty->waiting_thread != nullptr & tty->waiting_thread != thread) {
            // TODO: add support for multiple threads to wait to read
            release(&tty->lock);
            return -1;
        }

        tty->waiting_thread = thread;
        thread->status = WAITING;
        release(&tty->lock);
        thread->wait_until_taken_out_of_waiting();
    }
}

struct tty *tty_init(KeyboardDevice *keyboard_device, uint16_t priority_level) {
    if (keyboard_device == nullptr)
        return nullptr;

    struct tty *tty = reinterpret_cast<struct tty *>(calloc(1, sizeof(struct tty)));
    if (tty == nullptr)
        return nullptr;

    tty->keyboard_device = keyboard_device;
    tty->echo = putc;

    if (add_kernel_thread("tty", tty_routine, tty, priority_level) == nullptr) {
        free(tty);
        return nullptr;
    }

    return tty;
}