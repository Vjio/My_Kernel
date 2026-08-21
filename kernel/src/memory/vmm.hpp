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

    // maps a virtual page to a physical frame
    //
    // @param pml4          physical address of the page table to map into. 
    //                      set to nullptr to use the current process' own page table.
    // @param virtual_addr  virtual address where frame will be mapped. must be page-aligned!
    // @param physical_addr physical address of the frame you wish to map. set to 0
    //                      if you want a random, PMM supplied frame
    // @param flags         PTE flags applied to every page (PTE_PRESENT, PTE_USER, etc).
    //
    // @return              true if mapping succeeds. false if it fails
    static bool map_page(uint64_t *pml4, uint64_t virtual_addr, uint64_t physical_addr,
        uint64_t flags);

    // maps a run of virtually contiguous pages, backed by physical frames
    // requested by the VMM
    //
    // @param pml4          physical address of the page table to map into. 
    //                      set to nullptr to use the current process' own page table.
    // @param virtual_addr  first virtual address of the run. must be page-aligned!
    // @param nr_of_pages   number of consecutive 4KB pages to map.
    // @param flags         PTE flags applied to every page (PTE_PRESENT, PTE_USER, etc).
    //
    // @return              true if mapping succeeds. false if it fails
    //  
    // @note Frames come from the PMM and are NOT guaranteed to be
    //       physically contiguous. if you need specific virtual->physical
    //       mappings, use map_page() instead
    //
    // @return              true if mapping succeeds. false if it fails
    static bool map_pages(uint64_t *pml4, uint64_t virtual_addr,
        uint64_t nr_of_pages, uint64_t flags);
    // maps pages between old_end and new_end with the given flags
    // set pml4 to null if you want the page to be mapped in process own page map
    // make sure the pml4 is PHYSICAL not VIRTUAL
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
