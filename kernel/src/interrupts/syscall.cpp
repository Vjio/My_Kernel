#include "syscall.hpp"
#include "../memory/vmm.hpp"
#include "../memory/pmm.hpp"
#include "scheduling/process.hpp"
#include "scheduling/scheduler.hpp"
#include "stdio.hpp"

uint64_t clone(bool new_process_flag, char *name, uint64_t entry_point, void *arg) {
    // check if user given arguments are valid
    if (!VMM::validate_userland_memory(name, MAX_NAME_LEN, false))
        return static_cast<uint64_t>(-1);

    if (!VMM::validate_userland_memory(reinterpret_cast<void *>(entry_point), 1, false))
        return static_cast<uint64_t>(-1);

    Scheduler *scheduler = Scheduler::get_current_scheduler();
    struct thread *thread = scheduler->get_running_thread();
    if (new_process_flag)
        return create_user_process(name, entry_point, arg)->threads->tid;
    else
        return add_user_thread(thread->parent, name, entry_point, arg)->tid;
}

void exit() {
    struct thread *caller = Scheduler::get_current_scheduler()->get_running_thread();
    __asm__ volatile("sti");
    caller->thread_exit();
}

void sleep(uint64_t ticks) {
    struct thread *caller = Scheduler::get_current_scheduler()->get_running_thread();
    __asm__ volatile("sti");
    caller->thread_sleep(ticks);
}

uint64_t write(int fd, char *buf, size_t count) {
    if (!VMM::validate_userland_memory(buf, count, false))
        return static_cast<uint64_t>(-1);

    // two threads tring to write at the same time will currently lead to jumbled output
    // since i chose to reenable interrupts in this syscall (since the user can just
    // ask to print 1 billion characters andd that would freeze the system if interrupts are still masked)
    // TODO: fix by adding a lock
    __asm__ volatile("sti");
    for (size_t i = 0; i < count; i++)
        putc(buf[i]);
    return count;
}

struct brk_ret brk(size_t length) {
    if (length == 0)
        return {nullptr, 0};

    // round up to a whole number of pages
    uint64_t aligned_len = (length + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);

    thread *thread = Scheduler::get_current_scheduler()->get_running_thread();
    process *proc = thread->parent;

    acquire(&proc->lock);
    uint64_t old_heap_end = proc->heap_end;
    uint64_t new_heap_end = proc->heap_end + aligned_len;

    // brk should really only be called by a userland process
    // but just in case a mistake happen, i'll put this check here
    uint64_t flags = PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    if (thread->parent->is_kernel_process) {
        printf("don't use brk outside of userland! just call the VMM!\n");
        flags &= ~PTE_USER;
    }

    if (!VMM::map_pages(reinterpret_cast<uint64_t *>(proc->root_page_table), proc->heap_end, 
        aligned_len / FRAME_SIZE, flags)) {
        // TODO: update this once running out of memory is handled gracefully
        release(&proc->lock);
        return { nullptr, 0 };
    }

    proc->heap_end = new_heap_end;
    release(&proc->lock);

    return { reinterpret_cast<void *>(old_heap_end), aligned_len };
}
