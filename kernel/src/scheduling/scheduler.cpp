#include "scheduler.hpp"
#include "../memory/heap.hpp"

#define MAX_DYNAMIC_LEVEL 7   // 8th queue — highest level reachable by promotion
#define REALTIME_LOW  8       // 9th queue — fixed, never promotes/demotes
#define REALTIME_HIGH 9       // 10th queue — fixed, never promotes/demotes
#define PROMOTE_THRESHOLD_PCT 40

#define NTH_INT 5
#define BASE_INTERRUPT_NR   5

Scheduler *g_schedulers[MAX_CPUS] = { nullptr };

Scheduler *get_current_scheduler() {
    return g_schedulers[get_current_cpu_id()];
}

Scheduler::Scheduler() {
    Scheduler::interrupt_nr = 0;
}

void Scheduler::insert_process(struct process *process) {
    queue[process->current_level].push(process);
}

void Scheduler::schedule(struct interrupt_frame *frame) {
    if (running_proccess == nullptr)
        return;
    // update current running task
    running_proccess->int_frame = *frame;

    // handle every tick logic
    every_tick(frame);

    // handle every nth tick logic
    interrupt_nr++;
    if (interrupt_nr % NTH_INT != 0)
        return;

    every_n_tick();
}

void Scheduler::every_tick(struct interrupt_frame *frame) {
    bool must_switch = false;
    struct process *temp = nullptr;

    if (running_proccess->status == DEAD) {
        must_switch = true;
        temp = running_proccess;

    } else if (running_proccess->status == WAITING) {
        must_switch = true;
        reinsert();

    } else if (should_preempt()) {
        must_switch = true;
        running_proccess->status = READY;
        reinsert();

    } else {
        running_proccess->ttl--;
        if (running_proccess->ttl <= 0) {
            must_switch = true;
            running_proccess->status = READY;
            reinsert();
        }
    }

    if (!must_switch)
        return;

    running_proccess = find_next_task();
    running_proccess->status = RUNNING;
    *frame = running_proccess->int_frame;
    free(temp);
}

void Scheduler::every_n_tick() {
    for (int i = 0; i <= 6; i++) {
        if (queue[i].empty())
            continue;

        queue[i].promote_starving(this, interrupt_nr);
    }
}

struct process *Scheduler::check_high_prio() {
    for (int i = 9; i >= 8; i--) {
        if (queue[i].empty())
            continue;

        queue[i].clean_up();

        
        struct process *next_task = queue[i].extract_ready_process();
        next_task->status = RUNNING;
        next_task->ttl = BASE_INTERRUPT_NR;
        return next_task;
    }

    return nullptr;
}

struct process *Scheduler::find_next_task() {
    for (int i = 9; i >= 0; i--) {
        if (queue[i].empty())
            continue;

        queue[i].clean_up();
        
        struct process *next_task = queue[i].extract_ready_process();
        next_task->status = RUNNING;
        next_task->ttl = BASE_INTERRUPT_NR;
        return next_task;
    }

    return nullptr;
}

bool Scheduler::should_preempt() {
    // check if process can even be preempted
    if (running_proccess->current_level >= REALTIME_HIGH)
        return false;

    // only lvl 10 process can preempt a lvl 9
    if (running_proccess->current_level == REALTIME_LOW)
        return !queue[REALTIME_HIGH].empty();

    return !queue[REALTIME_LOW].empty() || !queue[REALTIME_HIGH].empty();
}

void Scheduler::reinsert() {
    // queues 9/10 are fixed, tasks there never promote or demote
    if (running_proccess->base_level <= MAX_DYNAMIC_LEVEL) {
        int used = BASE_INTERRUPT_NR - running_proccess->ttl;

        if (running_proccess->ttl <= 0) {
            // used its whole quantum -> demote
            if (running_proccess->base_level > 0)
                running_proccess->base_level--;
        } else if (used * 100 < PROMOTE_THRESHOLD_PCT * BASE_INTERRUPT_NR) {
            // gave up the CPU having used < 40% of its quantum -> promote
            if (running_proccess->base_level < MAX_DYNAMIC_LEVEL)
                running_proccess->base_level++;
        }
        // else, used 40-99% of quantum -> no change
    }

    // a starving task's current_level was bumped without touching base_level;
    // now that it's done running, it drops back to its base level
    running_proccess->current_level = running_proccess->base_level;
    running_proccess->ready_time = interrupt_nr;
    
    queue[running_proccess->base_level].push(running_proccess);
}
