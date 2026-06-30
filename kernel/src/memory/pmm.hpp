#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#define FRAME_SIZE   4096

typedef unsigned long paddr_t;

class PMM {
public:
    // could make this a singleton ig. for now this works
    PMM() = delete;

    // inits internal PMM structures
    // does nothing if PMM has already been init
    static void init_PMM(struct limine_memmap_response* memmap, uint64_t hhdm_offset);

    // allocates 1 frame
    static paddr_t alloc_frame();
    // allocates nr frames contiguously
    static paddr_t alloc_frames(uint64_t nr);

    // deallocates 1 frame
    static void free_frame(paddr_t address);
    // deallocates nr frames
    static void free_frames(paddr_t address, uint64_t nr);

private:
    // testing functions
    static uint64_t get_total_memory();
    static uint64_t get_free_memory();
    static uint64_t get_used_memory();

    // internal bitmap manipulation
    static void set_bit(uint64_t index);
    static void clear_bit(uint64_t index);
    static bool test_bit(uint64_t index);

    // internal variables
    static uint8_t *bitmap;
    static uint64_t bit_map_size;
    static uint64_t total_frames;
    static uint64_t free_frames;
    // index of the first byte with at least 1 free frame (at least 1 bit set to 0)
    // convention, if first_free_frame was set to a OXFF byte, that means there are no free frames
    static uint64_t first_free_frame;
};





