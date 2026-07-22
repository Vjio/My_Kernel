#include "process.hpp"
#include "../memory/heap.hpp"
#include "../memory/memory.hpp"
#include "../memory/vmm.hpp"
#include "scheduler.hpp"
#include "../interrupts/acpi.hpp"

size_t next_free_pid = 0;

struct process* create_process(char* name, void(*function)(void*), void* arg) {
    struct process* process = reinterpret_cast<struct process*>(malloc(sizeof(struct process)));

    memcpy(process->name, name, MAX_NAME_LEN);
    process->pid = next_free_pid++;
    process->status = READY;
    process->base_level = DEFAULT_LEVEL;
    process->current_level = DEFAULT_LEVEL;
    process->int_frame.cs = 0x08;
    process->int_frame.ss = 0x10;
    process->int_frame.rip = (uint64_t)function;
    process->int_frame.rdi = (uint64_t)arg;
    process->int_frame.rbp = 0;
    process->int_frame.rflags = 0x202;
    process->int_frame.rsp = reinterpret_cast<uint64_t>(malloc(STACK_SIZE));

    uint64_t pml4_phys = VMM::create_address_space();
    process->root_page_table = reinterpret_cast<void *>(pml4_phys);

    g_schedulers[get_current_cpu_id()]->insert_process(process);

    return process;
}
