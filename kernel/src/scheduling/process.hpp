#pragma once
#include "../interrupts/idt.hpp"
#include <cstddef>
#include "../locking/lock.h"

#define STARVING_TIME       1000
#define MAX_NAME_LEN        32
#define STACK_SIZE          FRAME_SIZE * 4
// highest address in thread's own half of the page table
#define USER_STACK_TOP      0x00007FFFFFFFF000ULL
#define STACK_GUARD_SIZE    FRAME_SIZE

// WAITING is currently unused
typedef enum {
    READY,
    RUNNING,
    WAITING,
    SLEEPING,
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
    uint64_t wake_time;
    int base_level;
    int current_level;
    // nr of timer interrupts left to do its work (quantum)
    int ttl;
    void* stack_base;
    // stack used whenever userland threads goes into ring 0
    void* kernel_stack;

    bool is_starving(uint64_t interrupt_nr) {
        return interrupt_nr - ready_time >= STARVING_TIME;
    }

    void thread_sleep(uint64_t ticks);
    void thread_exit();
};

struct process {
    void* root_page_table;
    struct thread* threads;
    size_t pid;
    size_t nr_of_threads;
    char name[MAX_NAME_LEN];
    struct spinlock lock;
};

inline void thread::thread_exit() {
    acquire(&parent->lock);
    if (prev_in_process != nullptr)
        prev_in_process->next_in_process = next_in_process;
    else
        parent->threads = next_in_process;

    if (next_in_process != nullptr)
        next_in_process->prev_in_process = prev_in_process;
    parent->nr_of_threads--;

    release(&parent->lock);
    status = DEAD;
    while (true) {;}
}

// creates a kernel process with 1 thread. notifies the scheduler about the thread
struct process *create_kernel_process(char *name, void(*function)(void*), void *arg);
// creates a user process with 1 thread. notifies the scheduler about the thread
struct process *create_user_process(char *name, uint64_t entry_point, void *arg);

// used by kernel. exposing this function to userspace will lead to creashes
// creates a thread and notifies the scheduler about it
// set proc to nullptr if you want to make a new process for the thread
struct thread* add_kernel_thread(struct process* proc, char* name, void(*function)(void*), void* arg);

// creates a thread for a userpsace process and notifies the scheduler about it
// set proc to nullptr if you want to make a new process for the thread
struct thread* add_user_thread(struct process* proc, char* name, uint64_t entry_point, void* arg);

// maps a process struct to the current running program
// will only be used by the kernel to make itself known to the scheduler
struct process *make_current_execution_process(char* name);
