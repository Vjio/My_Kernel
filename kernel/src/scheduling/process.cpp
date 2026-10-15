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
#include "../userland/elf_loader.hpp"

size_t next_free_pid = 0;
extern struct process kernel_process;

extern "C" uint64_t get_cr3();

// clean up functions

// undoes add_thread_common for a thread that didnt get linked
static void destroy_thread(struct process *proc, struct thread *thread) {
    acquire(&proc->lock);
    proc->nr_of_threads--;
    release(&proc->lock);

    free(thread->kernel_stack);
    free(thread);
}

// undoes create_process_common for a process that didnt get a thread
static void destroy_unstarted_process(struct process *process) {
    for (int i = 0; i < MAX_FILE_FD; i++) {
        struct file *file = process->fds[i];
        if (file == nullptr)
            continue;

        acquire(&file->lock);
        file->ref--;
        bool last = file->ref == 0;
        release(&file->lock);

        if (last) {
            process->fs->inode_return(file->ip->inode_nr);
            free(file);
        }
    }
    process->fs->inode_return(process->cwd->inode_nr);

    VMM::destroy_address_space(process->root_page_table);
    free(process);
}


// allocated memory and sets thread's inner flags
static struct thread *add_thread_common(struct process *proc, char *name) {
    struct thread* thread = reinterpret_cast<struct thread*>(calloc(1, sizeof(struct thread)));
    if (thread == nullptr)
        return nullptr;

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
    struct process *process = reinterpret_cast<struct process *>(calloc(1, sizeof(struct process)));
    if (process == nullptr)
        return nullptr;

    memcpy(process->name, name, MAX_NAME_LEN);
    process->pid = next_free_pid++;
    process->threads = nullptr;
    process->nr_of_threads = 0;
    process->next_stack_slot = 0;
    process->lock.locked = false;

    struct process *parent_proccess = Scheduler::get_current_scheduler()->get_running_thread()->parent;
    process->fs = parent_proccess->fs;
    process->cwd = process->fs->inode_get(parent_proccess->cwd->inode_nr);
    if (process->cwd == nullptr) {
        free(process);
        return nullptr;
    }

    uint64_t pml4_phys = VMM::create_address_space();
    if (pml4_phys == 0) {
        process->fs->inode_return(process->cwd->inode_nr);
        free(process);
        return nullptr;
    }
    process->root_page_table = reinterpret_cast<void *>(pml4_phys);

    // TODO: add stdin, stdout and stderr to every process

    acquire(&parent_proccess->lock);
    for (int i = 0; i < MAX_FILE_FD; i++) {
        if (parent_proccess->fds[i]) {
            parent_proccess->fds[i]->ref++;
            process->fds[i] = parent_proccess->fds[i];
        }
    }
    release(&parent_proccess->lock);

    return process;
}

struct thread *add_kernel_thread(char *name, void(*function)(void*),
    void *arg, unsigned int base_level) {
    struct process *proc = Scheduler::get_current_scheduler()->get_running_thread()->parent;
    acquire(&proc->lock);
    struct thread* thread = add_thread_common(proc, name);
    if (thread == nullptr) {
        release(&proc->lock);
        return nullptr;
    }

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
    if (stack == nullptr) {
        proc->nr_of_threads--;
        release(&proc->lock);
        free(thread);
        return nullptr;
    }

    thread->stack_base = stack;
    thread->int_frame.rsp = reinterpret_cast<uint64_t>(stack) + STACK_SIZE;
    thread->kernel_stack = nullptr;

    link_and_schedule(proc, thread);
    release(&proc->lock);

    return thread;
}

static struct thread *create_user_thread(struct process *proc, char *name,
    uint64_t entry_point, void *arg);

struct process *create_user_process(char *name, struct elf_source *src, void *arg) {
    struct process* process = create_process_common(name);
    if (process == nullptr)
        return nullptr;

    process->is_kernel_process = false;
    heap_init(VMM::get_hhdm_offset(), process);

    uint64_t entry;
    if (!load_elf(reinterpret_cast<uint64_t *>(process->root_page_table), src, &entry)) {
        destroy_unstarted_process(process);
        return nullptr;
    }

    struct thread *thread = create_user_thread(process, name, entry, arg);
    if (thread == nullptr) {
        destroy_unstarted_process(process);
        return nullptr;
    }

    // TODO: set up stack
    // later: write argv/envp onto the new stack and adjust thread->int_frame.rsp here

    acquire(&process->lock);
    link_and_schedule(process, thread);
    release(&process->lock);

    return process;
}

// creates a thread (allocated memory, fills the structure)
static struct thread *create_user_thread(struct process *proc, char *name,
        uint64_t entry_point, void *arg) {
    acquire(&proc->lock);
    struct thread *thread = add_thread_common(proc, name);
    if (thread == nullptr) {
        release(&proc->lock);
        return nullptr;
    }

    uint64_t slot = proc->next_stack_slot++;
    release(&proc->lock);

    thread->int_frame.cs = GDT_USER_CODE;
    thread->int_frame.ss = GDT_USER_DATA;
    thread->int_frame.rip = entry_point;
    thread->int_frame.rdi = (uint64_t)arg;

    // ring0 landing stack
    thread->kernel_stack = malloc(STACK_SIZE);
    if (thread->kernel_stack == nullptr) {
        destroy_thread(proc, thread);
        return nullptr;
    }

    // compute each thread stack's offset
    uint64_t stack_top = USER_STACK_TOP - (STACK_SIZE + STACK_GUARD_SIZE) * slot;
    // map ring3 stack
    if (!VMM::map_pages(reinterpret_cast<uint64_t *>(proc->root_page_table), stack_top - STACK_SIZE,
        STACK_SIZE / FRAME_SIZE, PTE_PRESENT | PTE_READ_WRITE | PTE_USER)) {
            printf("thread stack init failed!\n");
            destroy_thread(proc, thread);
            return nullptr;
    }
    thread->stack_base = reinterpret_cast<void*>(stack_top - STACK_SIZE);
    thread->int_frame.rsp = stack_top;

    return thread;
}

struct thread *add_user_thread(char *name, uint64_t entry_point, void *arg) {
    struct process *proc = Scheduler::get_current_scheduler()->get_running_thread()->parent;
    struct thread *thread = create_user_thread(proc, name, entry_point, arg);
    if (thread == nullptr)
        return nullptr;

    acquire(&proc->lock);
    link_and_schedule(proc, thread);
    release(&proc->lock);

    return thread;
}

static struct thread *make_current_execution_thread(char *name, struct process *parent) {
    struct thread *thread = reinterpret_cast<struct thread*>(calloc(1, (sizeof(struct thread))));

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
    proc->next_stack_slot = 0;
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

void thread::wait_until_taken_out_of_waiting() {
    // printf("PUT THREAD INTO WAITING\n");
    while(status == WAITING);
}

void thread::take_thread_out_of_waiting() {
    // printf("TAKE THREAD OUT OF WAITING\n");
    ready_time = Scheduler::get_current_scheduler()->get_interrupt_nr();
    status = READY;
}

int find_first_free_fd(struct process *proc) {
    // start from 3 to jump over in, out, err
    for (int i = 3; i < MAX_FILE_FD; i++)
        if (!proc->fds[i])
            return i;
    return -1;
}
