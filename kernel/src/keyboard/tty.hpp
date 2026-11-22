#pragma once
#include <cstdint>
#include <cstddef>
#include "spinlock.h"
#include "keyboard/keyboard_device.hpp"
#include "keyboard/keymap.hpp"

// longest line user can edit
#define TTY_LINE_MAX    256
// committed bytes waiting to be read
#define TTY_READY_MAX   1024

// TTY will hold two buffers, the current line being written
// and a buffer of past written lines (this buffer will work like a very simple ring buffer)
// every ASCII character gets written to the console and to the line buffer

struct tty {
    KeyboardDevice *keyboard_device;
    struct keyboard_state keyboard_state;

    // defaults to putc
    void (*echo)(char c);

    char line[TTY_LINE_MAX];
    size_t line_len;

    char ready[TTY_READY_MAX];
    size_t ready_head;
    size_t ready_len;
    bool eof;

    struct spinlock lock;
    struct thread *waiting_thread;
};

void tty_handle_event(struct tty *tty, const key_event &event);

// echos the character to the console and writes it to the line buffer
// flushes line buffer if needed
void tty_input_char(struct tty *tty, char c);

// reads up to n bytes or until \n from tty ready buffer
// blocks until data is available to be read
//
// returns the number of bytes read
// returns 0 on end of file
// returns -1 if read failed
long tty_read(struct tty *tty, char *buf, int n);

// creates the tty and starts its thread. returns nullptr on failure
struct tty *tty_init(KeyboardDevice *keyboard_device, uint16_t priority_level);