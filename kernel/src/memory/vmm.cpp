#include "pmm.hpp"
#include "vmm.hpp"
#include "memory.hpp"

extern "C" uint64_t get_cr3();
extern "C" void flush_tlb(uint64_t addr);

uint64_t *VMM::get_pml4(uint64_t hhdm_offset) {
    uint64_t cr3 = get_cr3();

    // bits 0-11 are asummed to be 0 but can sometimes be used for flags
    // and them with 0 to avoid any issues
    uint64_t pml4_phys = cr3 & ~0xFFFull;
    // cr3 holds the phys address, so we must add the hhdm offset before returning it
    return reinterpret_cast<uint64_t *>(pml4_phys + hhdm_offset);
}

uint64_t *VMM::get_page_table_index(uint64_t* table, uint64_t index, uint64_t hhdm_offset) {
    if ((table[index] & PTE_PRESENT) == 0) {
        // index hasn't been allocated. ask pmm for a new frame
        uint64_t new_table_phys = PMM::alloc_frame();

        // add hhdm offset
        uint64_t *new_table_virt = reinterpret_cast<uint64_t *>(new_table_phys + hhdm_offset);

        // set page table to 0
        memset(new_table_virt, 0, FRAME_SIZE);

        table[index] = new_table_phys | PTE_PRESENT | PTE_READ_WRITE;
    }
    // ret the virtual address of the next level
    uint64_t next_level_phys = table[index] & ~0xFFFull;
    return reinterpret_cast<uint64_t *>(next_level_phys + hhdm_offset);
}

void VMM::map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags, uint64_t hhdm_offset) {
    uint64_t* pml4 = VMM::get_pml4(hhdm_offset);

    // extract each index (https://blog.xenoscr.net/resources/images/2021-09-06-Exploring-Virtual-Memory-and-Page-Structures/image-20210831220831378.png)
    uint64_t pml4_index = (virtual_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virtual_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virtual_addr >> 12) & 0x1FF;

    uint64_t *pdpt = get_page_table_index(pml4, pml4_index, hhdm_offset);
    uint64_t *pd   = get_page_table_index(pdpt, pdpt_index, hhdm_offset);
    uint64_t *pt   = get_page_table_index(pd, pd_index, hhdm_offset);

    // put phys addr along with flags
    pt[pt_index] = (physical_addr & ~0xFFFull) | flags;

    // flush TBL since we just invalidated whatever was at that address
    flush_tlb(virtual_addr);
}
