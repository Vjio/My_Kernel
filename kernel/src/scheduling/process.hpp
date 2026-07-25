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

struct process;

struct thread {
    struct interrupt_frame int_frame;
    size_t tid;
    status_t status;
    char name[MAX_NAME_LEN];
    // pointer to next thread inside scheduler queue
    struct thread* next;
    // pointer to next thread that belongs to this thread's process
    struct thread* next_in_process;
    // pointer to previous thread
    struct thread* prev_in_process;
    // process that this thread belongs to
    struct process* parent;
    // last exact date the thread became ready
    // used to check if a thread is starving
    uint64_t ready_time;
    int base_level;
    int current_level;
    // nr of timer interrupts left to do its work (quantum)
    int ttl;
    void* stack_base;

    bool is_starving(uint64_t interrupt_nr) {
        return interrupt_nr - ready_time >= STARVING_TIME;
    }

    void thread_exit();
};

struct process {
    void* root_page_table;
    struct thread* threads;
    size_t pid;
    char name[MAX_NAME_LEN];
};

inline void thread::thread_exit() {
    if (prev_in_process != nullptr)
        prev_in_process->next_in_process = next_in_process;
    else
        parent->threads = next_in_process;

    if (next_in_process != nullptr)
        next_in_process->prev_in_process = prev_in_process;

    if (parent->threads == nullptr)
        // convetion, if process threads points to a dead thread
        // process has no other threads to run and must be freed
        parent->threads = this;

    status = DEAD;
    while (true) {;}
}

// creates a thread and notifies the scheduler about it
// set proc to nullptr if you want to make a new process for the thread
struct thread* add_thread(struct process* proc, char* name, void(*function)(void*), void* arg);
// creates a process with one thread. notifies the scheduler about the created thread
struct process* create_process(char* name, void(*function)(void*), void* arg);
// maps a process struct to the current running program
// will only be used by the kernel to make itself known to the scheduler
struct process *make_current_execution_process(char* name);
