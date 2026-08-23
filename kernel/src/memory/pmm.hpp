#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "../locking/lock.h"

#define FRAME_SIZE   4096

typedef unsigned long paddr_t;

// TODO: add synchronization primitives once multi thread support is implemented 
namespace PMM {
    // inits internal PMM structures
    // does nothing if PMM has already been init
    void init_PMM(struct limine_memmap_response* memmap, uint64_t hhdm_offset);

    // allocates 1 frame
    // returns 0 if allocation failes
    paddr_t alloc_frame();
    // allocates nr frames contiguously
    // returns 0 if allocation failes
    paddr_t alloc_frames(uint64_t nr);

    // deallocates 1 frame
    void free_frame(paddr_t address);
    // deallocates nr frames
    void free_frames(paddr_t address, uint64_t nr);

    // testing functions
    uint64_t get_total_memory();
    uint64_t get_free_memory();
    uint64_t get_used_memory();
};
