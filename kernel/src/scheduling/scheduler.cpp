#include "scheduler.hpp"
#include "memory/heap.hpp"
#include "memory/vmm.hpp"
#include "gdt.hpp"
#include "interrupts/tss.hpp"

#define PROMOTE_THRESHOLD_PCT 40

#define NTH_INT 5
#define BASE_INTERRUPT_NR   5

extern "C" void load_cr3(uint64_t new_cr3);
extern "C" uint64_t get_cr3();
void dummy_work(void *);

extern struct tss_entry tss;

Scheduler *g_schedulers[MAX_CPUS] = { nullptr };

Scheduler::Scheduler(char *name) {
    g_schedulers[get_current_cpu_id()] = this;
    Scheduler::interrupt_nr = 0;
    Scheduler::dummy = create_kernel_process("dummy", dummy_work, nullptr)->threads;
    running_thread = make_current_execution_process(name)->threads;
}

Scheduler *Scheduler::get_current_scheduler() {
    if (g_schedulers[get_current_cpu_id()] != nullptr)
        return g_schedulers[get_current_cpu_id()];
    
    g_schedulers[get_current_cpu_id()] = new Scheduler("kernel_process");
    return g_schedulers[get_current_cpu_id()] ;
}

void Scheduler::insert_thread(struct thread *thread) {
    queue[thread->current_level].push(thread);
}

void Scheduler::schedule(struct interrupt_frame *frame) {
    // update current running task
    if (running_thread != nullptr)
        running_thread->int_frame = *frame;

    // handle every tick logic
    every_tick(frame);

    // handle every nth tick logic
    interrupt_nr++;
    if (interrupt_nr % NTH_INT != 0)
        return;

    every_n_tick();
}

uint64_t Scheduler::get_interrupt_nr() {
    return this->interrupt_nr;
}

void Scheduler::add_to_sleep_list(struct thread *thread) {
    thread->next = sleep_list;
    sleep_list = thread;
}

void Scheduler::wake_sleeping() {
    struct thread **cur = &sleep_list;

    while (*cur != nullptr) {
        struct thread *thread = *cur;
        if (thread->wake_time <= interrupt_nr) {
            *cur = thread->next;
            thread->status = READY;
            thread->ready_time = interrupt_nr;
            insert_thread(thread);
        } else {
            cur = &thread->next;
        }
    }
}

void Scheduler::every_tick(struct interrupt_frame *frame) {
    struct thread *dead_thread = nullptr;
    struct process *dead_process = nullptr;

    if (running_thread != nullptr) [[likely]] {
        bool must_switch = false;
        if (running_thread->status == DEAD) {
            must_switch = true;
            dead_thread = running_thread;
            // convetion, if process threads points to a dead thread
            // process has no other threads to run and must be freed
            dead_process = running_thread->parent;

        } else if (running_thread->status == WAITING) {
            must_switch = true;
            reinsert();

        } else if (running_thread->status == SLEEPING) {
            must_switch = true;
            add_to_sleep_list(running_thread);

        } else if (should_preempt()) {
            must_switch = true;
            running_thread->status = READY;
            reinsert();

        } else {
            running_thread->ttl--;
            if (running_thread->ttl <= 0) {
                must_switch = true;
                running_thread->status = READY;
                reinsert();
            }
        }

        if (!must_switch)
            return;
    }

    running_thread = find_next_task();
    if (running_thread == nullptr)
        running_thread = dummy;

    uint64_t new_cr3 = reinterpret_cast<uint64_t>(running_thread->parent->root_page_table);
    if (new_cr3 != get_cr3())
        load_cr3(new_cr3);
    running_thread->status = RUNNING;
    *frame = running_thread->int_frame;

    // point the TSS at this thread's kernel-mode landing stack so the
    // next ring3->ring0 transition (timer, fault, syscall) has somewhere to push to
    if (running_thread->kernel_stack != nullptr) {
        tss.rsp[0] = reinterpret_cast<uint64_t>(running_thread->kernel_stack) + STACK_SIZE;
    }

    if (dead_thread != nullptr)
        free(dead_thread);

    if (dead_process != nullptr) {
        VMM::destroy_address_space(dead_process->root_page_table);
        free(dead_process);
    }
}

void Scheduler::every_n_tick() {
    for (int i = 0; i <= 6; i++) {
        if (queue[i].empty())
            continue;

        queue[i].promote_starving(this, interrupt_nr);
    }
    wake_sleeping();
}

struct thread *Scheduler::check_high_prio() {
    for (int i = 9; i >= 8; i--) {
        if (queue[i].empty())
            continue;

        queue[i].clean_up();
        
        struct thread *next_task = queue[i].extract_ready_thread();
        next_task->status = RUNNING;
        next_task->ttl = BASE_INTERRUPT_NR;
        return next_task;
    }

    return nullptr;
}

struct thread *Scheduler::find_next_task() {
    for (int i = 9; i >= 0; i--) {
        if (queue[i].empty())
            continue;

        queue[i].clean_up();
        
        struct thread *next_task = queue[i].extract_ready_thread();
        next_task->status = RUNNING;
        next_task->ttl = BASE_INTERRUPT_NR;
        return next_task;
    }

    return nullptr;
}

bool Scheduler::should_preempt() {
    // check if process can even be preempted
    if (running_thread->current_level >= REALTIME_HIGH)
        return false;

    // only lvl 10 process can preempt a lvl 9
    if (running_thread->current_level == REALTIME_LOW)
        return !queue[REALTIME_HIGH].empty();

    return !queue[REALTIME_LOW].empty() || !queue[REALTIME_HIGH].empty();
}

void Scheduler::reinsert() {
    // queues 9/10 are fixed, tasks there never promote or demote
    if (running_thread->base_level <= MAX_DYNAMIC_LEVEL) {
        int used = BASE_INTERRUPT_NR - running_thread->ttl;

        if (running_thread->ttl <= 0) {
            // used its whole quantum -> demote
            if (running_thread->base_level > 0)
                running_thread->base_level--;
        } else if (used * 100 < PROMOTE_THRESHOLD_PCT * BASE_INTERRUPT_NR) {
            // gave up the CPU having used < 40% of its quantum -> promote
            if (running_thread->base_level < MAX_DYNAMIC_LEVEL)
                running_thread->base_level++;
        }
        // else, used 40-99% of quantum -> no change
    }

    // a starving task's current_level was bumped without touching base_level;
    // now that it's done running, it drops back to its base level
    running_thread->current_level = running_thread->base_level;
    running_thread->ready_time = interrupt_nr;
    
    queue[running_thread->base_level].push(running_thread);
}

// dummy function. only use for spawning dummy process
// so that the scheduler will always have at least 1 thread to run
void dummy_work(void *) {
    while (true)
        asm("hlt");
}
