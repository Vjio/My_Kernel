#include "process.hpp"
#include "../memory/heap.hpp"
#include "../memory/memory.hpp"
#include "../memory/vmm.hpp"
#include "scheduler.hpp"
#include "../interrupts/acpi.hpp"

size_t next_free_pid = 0;

struct thread* add_thread(struct process* proc, char* name, void(*function)(void*), void* arg) {
    if (proc == nullptr) {
        create_process(name, function, arg);
        return proc->threads;
    }

    struct thread* thread = reinterpret_cast<struct thread*>(malloc(sizeof(struct thread)));

    memcpy(thread->name, name, MAX_NAME_LEN);
    thread->tid = next_free_pid++;
    thread->status = READY;
    thread->base_level = DEFAULT_LEVEL;
    thread->current_level = DEFAULT_LEVEL;
    thread->parent = proc;
    thread->int_frame.cs = 0x08;
    thread->int_frame.ss = 0x10;
    thread->int_frame.rip = (uint64_t)function;
    thread->int_frame.rdi = (uint64_t)arg;
    thread->int_frame.rbp = 0;
    thread->int_frame.rflags = 0x202;

    void *stack = malloc(STACK_SIZE);
    thread->stack_base = stack;
    thread->int_frame.rsp = reinterpret_cast<uint64_t>(stack) + STACK_SIZE;

    // push to front of list of threads
    thread->next_in_process = proc->threads;
    if (proc->threads)
        proc->threads->prev_in_process = thread;
    proc->threads = thread;

    thread->next = nullptr;
    g_schedulers[get_current_cpu_id()]->insert_thread(thread);

    return thread;
}

struct process* create_process(char* name, void(*function)(void*), void* arg) {
    struct process* process = reinterpret_cast<struct process*>(malloc(sizeof(struct process)));

    memcpy(process->name, name, MAX_NAME_LEN);
    process->pid = next_free_pid++;
    process->threads = nullptr;

    uint64_t pml4_phys = VMM::create_address_space();
    process->root_page_table = reinterpret_cast<void *>(pml4_phys);

    add_thread(process, name, function, arg);

    return process;
}
