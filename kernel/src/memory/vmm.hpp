#pragma once
#include <stdint.h>
#include <stddef.h>

#define PTE_PRESENT          0b00001
#define PTE_READ_WRITE       0b00010
#define PTE_USER             0b00100
#define PTE_WRITE_THROUGH    0b01000
#define PTE_CACHE_DISABLE    0b10000

class VMM {
    public:
    VMM() = delete;

    static void init(uint64_t hhdm_offset);
    // retrieve current active page table
    static uint64_t *get_pml4();
    // maps a 4KB virtual page to a 4KB physical frame
    // set pml4 to null if you want the page to be mapped in process own page map
    static void map_page(uint64_t *pml4, uint64_t virtual_addr, uint64_t physical_addr,
        uint64_t flags);
    // maps pages between old_end and new_end with the given flags
    // set pml4 to null if you want the page to be mapped in process own page map
    // returns false if mapping failes
    static bool grow_region(uint64_t *pml4, uint64_t old_end, uint64_t new_end,
            uint64_t flags);
    // reverse of map page, frees a mapped page
    static void unmap_page(uint64_t *pml4, uint64_t virtual_addr);

    static uint64_t create_address_space();
    static void destroy_address_space(void* root_page_table);
    static uint64_t get_hhdm_offset();
    // validate that the given memory address is mappend and belongs to userland memory
    // true -> address is valid
    // flase -> address is invalid
    static bool validate_userland_memory(void *address, size_t length, bool require_write);

    private:
    // get value at table's index
    // alocates a new frame for the index if needed
    static uint64_t *get_page_table_index(uint64_t* table, uint64_t index);
    static void free_table(uint64_t table_phys, int level);
};
