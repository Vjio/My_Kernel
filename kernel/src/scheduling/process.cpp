#include "process.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "memory/vmm.hpp"
#include "memory/pmm.hpp"
#include "scheduler.hpp"
#include "interrupts/acpi.hpp"
#include "gdt.hpp"
#include "interrupts/tss.hpp"
#include "../stdio.hpp"

size_t next_free_pid = 0;
extern struct process kernel_process;

extern "C" uint64_t get_cr3();

// allocated memory and sets thread's inner flags
static struct thread *add_thread_common(struct process *proc, char *name) {
    struct thread* thread = reinterpret_cast<struct thread*>(malloc(sizeof(struct thread)));

    memcpy(thread->name, name, MAX_NAME_LEN);
    thread->tid = next_free_pid++;
    thread->status = READY;
    thread->base_level = DEFAULT_LEVEL;
    thread->current_level = DEFAULT_LEVEL;
    thread->parent = proc;
    thread->int_frame.rbp = 0;
    thread->int_frame.rflags = 0x202;
    thread->parent->nr_of_threads++;

    return thread;
}

// updates thread inner list and notifies scheduler of it
// only call this after thread is fully made
static void link_and_schedule(struct process *proc, struct thread *thread) {
    // push to front of list of threads
    thread->next_in_process = proc->threads;
    if (proc->threads)
        proc->threads->prev_in_process = thread;
    proc->threads = thread;

    thread->next = nullptr;
    Scheduler::get_current_scheduler()->insert_thread(thread);
}

struct process *create_process_common(char *name) {
    struct process *process = reinterpret_cast<struct process *>(malloc(sizeof(struct process)));

    memcpy(process->name, name, MAX_NAME_LEN);
    process->pid = next_free_pid++;
    process->threads = nullptr;
    process->nr_of_threads = 0;
    process->lock.locked = false;

    uint64_t pml4_phys = VMM::create_address_space();
    process->root_page_table = reinterpret_cast<void *>(pml4_phys);

    return process;
}

struct process *create_kernel_process(char* name, void(*function)(void*), void* arg) {
    struct process* process = create_process_common(name);

    add_kernel_thread(process, name, function, arg, DEFAULT_LEVEL);
    process->is_kernel_process = true;
    heap_init(VMM::get_hhdm_offset(), process);

    return process;
}

struct process *create_user_process(char *name, uint64_t entry_point, void *arg) {
    struct process* process = create_process_common(name);

    add_user_thread(process, name, entry_point, arg);
    process->is_kernel_process = false;
    heap_init(VMM::get_hhdm_offset(), process);

    return process;
}

struct thread *add_kernel_thread(struct process *proc, char *name, void(*function)(void*), void *arg, unsigned int base_level) {
    if (proc == nullptr) {
        proc = create_kernel_process(name, function, arg);
        return proc->threads;
    }

    acquire(&proc->lock);
    struct thread* thread = add_thread_common(proc, name);

    thread->int_frame.cs = GDT_KERNEL_CODE;
    thread->int_frame.ss = GDT_KERNEL_DATA;
    thread->int_frame.rip = (uint64_t)function;
    thread->int_frame.rdi = (uint64_t)arg;
    if (base_level > REALTIME_HIGH)
        base_level = REALTIME_HIGH;
    thread->base_level = base_level;
    thread->current_level = base_level;

    // kernel threads never change privilege level, so they only ever need one stack
    void* stack = malloc(STACK_SIZE);
    thread->stack_base = stack;
    thread->int_frame.rsp = reinterpret_cast<uint64_t>(stack) + STACK_SIZE;
    thread->kernel_stack = nullptr;

    link_and_schedule(proc, thread);
    release(&proc->lock);

    return thread;
}

struct thread *add_user_thread(struct process *proc, char *name, uint64_t entry_point, void *arg) {
    if (proc == nullptr) {
        proc = create_user_process(name, entry_point, arg);
        return proc->threads;
    }

    acquire(&proc->lock);
    struct thread *thread = add_thread_common(proc, name);

    thread->int_frame.cs = GDT_USER_CODE;
    thread->int_frame.ss = GDT_USER_DATA;
    thread->int_frame.rip = entry_point;
    thread->int_frame.rdi = (uint64_t)arg;

    // ring0 landing stack
    thread->kernel_stack = malloc(STACK_SIZE);

    // compute each thread stack's offset
    uint64_t stack_top = USER_STACK_TOP - (STACK_SIZE + STACK_GUARD_SIZE) *
        (thread->parent->nr_of_threads - 1);
    // map ring3 stack
    if (!VMM::map_pages(reinterpret_cast<uint64_t *>(proc->root_page_table), stack_top - STACK_SIZE,
        STACK_SIZE / FRAME_SIZE, PTE_PRESENT | PTE_READ_WRITE | PTE_USER)) {
            // TODO: handle stack allocation failure gracefully
            printf("thread stack init failed!\n");
            while (true) {;}
    }
    thread->stack_base = reinterpret_cast<void*>(stack_top - STACK_SIZE);
    thread->int_frame.rsp = stack_top;

    link_and_schedule(proc, thread);
    release(&proc->lock);

    return thread;
}

static struct thread *make_current_execution_thread(char *name, struct process *parent) {
    struct thread *thread = reinterpret_cast<struct thread*>(malloc(sizeof(struct thread)));

    memcpy(thread->name, name, MAX_NAME_LEN);
    thread->tid = next_free_pid++;
    thread->status = RUNNING;
    thread->base_level = REALTIME_HIGH;
    thread->current_level = REALTIME_HIGH;
    thread->parent = parent;
    thread->next = nullptr;
    thread->next_in_process = nullptr;
    thread->prev_in_process = nullptr;
    // int_frame deliberately left unset. schedule() populates it
    // the first time this thread is preempted

    return thread;
}

struct process *make_current_execution_process(char* name) {
    struct process* process = &kernel_process;

    memcpy(process->name, name, MAX_NAME_LEN);

    process->threads = make_current_execution_thread("kernel_thread", process);

    return process;
}

void populate_kernel_process_struct(struct process *proc) {
    proc->root_page_table = reinterpret_cast<void *>(get_cr3() & ~0xFFFull);
    // heap_end gets set by heap_init()
    proc->heap_end = 0;
    proc->threads = nullptr;
    proc->nr_of_threads = 0;
    proc->pid = next_free_pid++;
    proc->lock.locked = false;
    proc->is_kernel_process = true;
}

void thread::thread_sleep(uint64_t ticks) {
    Scheduler *scheduler = Scheduler::get_current_scheduler();

    wake_time = scheduler->get_interrupt_nr() + ticks;
    status = SLEEPING;

    // Block here until the scheduler wakes thread up
    while (status != RUNNING) {
        asm volatile("hlt");
    }
}

void thread::put_thread_into_waiting() {
    printf("PUT THREAD INTO WAITING\n");
    status = WAITING;
    while(status == WAITING);
}

void thread::take_thread_out_of_waiting() {
    printf("TAKE THREAD OUT OF WAITING\n");
    ready_time = Scheduler::get_current_scheduler()->get_interrupt_nr();
    status = READY;
}
