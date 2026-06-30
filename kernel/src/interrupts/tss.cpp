#include "tss.hpp"

// for now we will allocate this in bss
__attribute__((aligned(16))) 
static uint8_t double_fault_stack[4096];

struct tss_entry tss = {};

void tss_init() {
    // pass a pointer to the top of the stack
    // (so that it can grow downwards)
    tss.ist[0] = reinterpret_cast<uint64_t>(double_fault_stack) + sizeof(double_fault_stack);

    // disable the I/O map base by pointing it past the end of the TSS
    tss.iomap_base = sizeof(tss_entry);
}
