#pragma once
#include "../interrupts/idt.hpp"

#define STARVING_TIME   1000

typedef enum {
    READY,
    RUNNING,
    WAITING,
    DEAD
} status_t;

struct process {
    size_t pid;
    struct interrupt_frame int_frame;
    void* root_page_table;
    struct process *next;
    int base_level;
    int current_level;
    // last exact date the process became ready
    // used to check if a process is starving
    uint64_t ready_time;
    // nr of timer interrupts left to do its work (quantum)
    int ttl;
    status_t status;

    bool is_starving(uint64_t interrupt_nr) {
        return interrupt_nr - ready_time >= STARVING_TIME;
    }
};
