#pragma once
#include "scheduler_queue.hpp"
#include "process.hpp"
#include "../interrupts/acpi.hpp"

// task == process

// each CPU will eventually have their own scheduler
// this is to avoid having costly locks on the internal process queues
#define MAX_CPUS 4
#define DEFAULT_LEVEL   7

// singleton object that handles schedulling for a core
class Scheduler {
    public:
    Scheduler();
    void schedule(struct interrupt_frame *frame);
    void insert_process(struct process *process);

    private:
    uint64_t interrupt_nr = 0;
    // current running proccess on the CPU
    struct process *running_proccess = nullptr;
    SchedulerQueue queue[10];

    // function called every clock interrupt
    void every_tick(struct interrupt_frame *frame);
    // function called every n clock interrupts
    void every_n_tick();
    // returns a pointer to a higher prio task ready to be ran
    // returns null if no such tasks exist
    struct process* check_high_prio();
    // returns the next highest prio ready task
    struct process *find_next_task();
    // inserts current running proccess back into its queue
    void reinsert();
    // returns true if process should be kicked off cpu
    bool should_preempt();
};

extern Scheduler *g_schedulers[MAX_CPUS];
Scheduler *get_current_scheduler();
