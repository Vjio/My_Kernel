#pragma once
#include "../interrupts/idt.hpp"
#include <cstddef>

#define STARVING_TIME   1000
#define MAX_NAME_LEN    32
#define STACK_SIZE      FRAME_SIZE * 4

typedef enum {
    READY,
    RUNNING,
    WAITING,
    DEAD
} status_t;

struct process {
    struct interrupt_frame int_frame;
    void* root_page_table;
    struct process *next;
    // last exact date the process became ready
    // used to check if a process is starving
    uint64_t ready_time;
    size_t pid;
    int base_level;
    int current_level;
    // nr of timer interrupts left to do its work (quantum)
    int ttl;
    status_t status;
    char name[MAX_NAME_LEN];

    bool is_starving(uint64_t interrupt_nr) {
        return interrupt_nr - ready_time >= STARVING_TIME;
    }
};
