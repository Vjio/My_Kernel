#include "pmm.hpp"
#include "vmm.hpp"
#include "memory.hpp"

uint64_t l_hhdm_offset    = 0;
uint64_t kernel_pml4_phys = 0;

extern "C" uint64_t get_cr3();
extern "C" void flush_tlb(uint64_t addr);

void VMM::init(uint64_t hhdm_offset) {
    l_hhdm_offset = hhdm_offset;
    kernel_pml4_phys = get_cr3() & ~0xFFFull;
}

uint64_t *VMM::get_pml4(uint64_t hhdm_offset) {
    uint64_t cr3 = get_cr3();

    // bits 0-11 are asummed to be 0 but can sometimes be used for flags
    // and them with 0 to avoid any issues
    uint64_t pml4_phys = cr3 & ~0xFFFull;
    // cr3 holds the phys address, so we must add the hhdm offset before returning it
    return reinterpret_cast<uint64_t *>(pml4_phys + hhdm_offset);
}

uint64_t VMM::get_hhdm_offset() {
    return l_hhdm_offset;
}

uint64_t *VMM::get_page_table_index(uint64_t* table, uint64_t index, uint64_t hhdm_offset) {
    if ((table[index] & PTE_PRESENT) == 0) {
        // index hasn't been allocated. ask pmm for a new frame
        uint64_t new_table_phys = PMM::alloc_frame();

        // add hhdm offset
        uint64_t *new_table_virt = reinterpret_cast<uint64_t *>(new_table_phys + hhdm_offset);

        // set page table to 0
        memset(new_table_virt, 0, FRAME_SIZE);

        table[index] = new_table_phys | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    }
    // ret the virtual address of the next level
    uint64_t next_level_phys = table[index] & ~0xFFFull;
    return reinterpret_cast<uint64_t *>(next_level_phys + hhdm_offset);
}

void VMM::map_page(uint64_t *pml4, uint64_t virtual_addr, uint64_t physical_addr,
    uint64_t flags, uint64_t hhdm_offset) {
    if (pml4 == nullptr)
        pml4 = VMM::get_pml4(hhdm_offset);

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

uint64_t VMM::create_address_space() {
    uint64_t new_pml4_phys = PMM::alloc_frame();
    uint64_t *new_pml4_virt = reinterpret_cast<uint64_t *>(new_pml4_phys + l_hhdm_offset);
    memset(new_pml4_virt, 0, FRAME_SIZE);

    uint64_t *kernel_pml4_virt = reinterpret_cast<uint64_t *>(kernel_pml4_phys + l_hhdm_offset);

    // indices 256-511 = higher half (kernel image + HHDM). every address space shares these
    for (int i = 256; i < 512; i++)
        new_pml4_virt[i] = kernel_pml4_virt[i];
    // indices 0-255 (user half) are left zeroed; the process fills these in itself

    return new_pml4_phys;
}

void VMM::destroy_address_space(void *root_page_table) {
    uint64_t pml4_phys = reinterpret_cast<uint64_t>(root_page_table);
    uint64_t *pml4_virt = reinterpret_cast<uint64_t *>(pml4_phys + l_hhdm_offset);

    for (int i = 0; i < 256; i++) {
        if ((pml4_virt[i] & PTE_PRESENT) == 0)
            continue;

        uint64_t pdpt_phys = pml4_virt[i] & ~0xFFFull;
        free_table(pdpt_phys, 2);
    }
    // indices 256-511 point at the shared kernel/HHDM tables. don't destroy them

    PMM::free_frame(pml4_phys);
}

void VMM::free_table(uint64_t table_phys, int level) {
    uint64_t *table_virt = reinterpret_cast<uint64_t *>(table_phys + l_hhdm_offset);

    for (int i = 0; i < 512; i++) {
        if ((table_virt[i] & PTE_PRESENT) == 0)
            continue;

        uint64_t next_phys = table_virt[i] & ~0xFFFull;
        if (level > 0)
            free_table(next_phys, level - 1);
        else
            // an actual data frame, not a table
            PMM::free_frame(next_phys);
    }

    PMM::free_frame(table_phys);
}

