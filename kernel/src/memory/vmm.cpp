#include "pmm.hpp"
#include "vmm.hpp"
#include "memory.hpp"

uint64_t l_hhdm_offset    = 0;
uint64_t kernel_pml4_phys = 0;

extern "C" uint64_t get_cr3();
extern "C" void flush_tlb(uint64_t addr);

static constexpr uint64_t USER_SPACE_TOP = 0x0000800000000000ull;

void VMM::init(uint64_t hhdm_offset) {
    l_hhdm_offset = hhdm_offset;
    kernel_pml4_phys = get_cr3() & ~0xFFFull;
}

uint64_t *VMM::get_pml4() {
    uint64_t cr3 = get_cr3();

    // bits 0-11 are asummed to be 0 but can sometimes be used for flags
    // and them with 0 to avoid any issues
    uint64_t pml4_phys = cr3 & ~0xFFFull;
    // cr3 holds the phys address, so we must add the hhdm offset before returning it
    return reinterpret_cast<uint64_t *>(pml4_phys + l_hhdm_offset);
}

uint64_t VMM::get_hhdm_offset() {
    return l_hhdm_offset;
}

// returns the address of the next table or nullptr if it doesn't exist
static uint64_t *peek_table(uint64_t *table, uint64_t index) {
    if ((table[index] & PTE_PRESENT) == 0)
        return nullptr;
    uint64_t next_phys = table[index] & ~0xFFFull;
    return reinterpret_cast<uint64_t *>(next_phys + l_hhdm_offset);
}

uint64_t *VMM::get_page_table_index(uint64_t* table, uint64_t index) {
    if ((table[index] & PTE_PRESENT) == 0) {
        // index hasn't been allocated. ask pmm for a new frame
        uint64_t new_table_phys = PMM::alloc_frame();

        // add hhdm offset
        uint64_t *new_table_virt = reinterpret_cast<uint64_t *>(new_table_phys + l_hhdm_offset);

        // set page table to 0
        memset(new_table_virt, 0, FRAME_SIZE);

        table[index] = new_table_phys | PTE_PRESENT | PTE_READ_WRITE | PTE_USER;
    }
    // ret the virtual address of the next level
    uint64_t next_level_phys = table[index] & ~0xFFFull;
    return reinterpret_cast<uint64_t *>(next_level_phys + l_hhdm_offset);
}

void VMM::map_page(uint64_t *pml4, uint64_t virtual_addr, uint64_t physical_addr,
    uint64_t flags) {
    if (pml4 == nullptr)
        pml4 = VMM::get_pml4();

    // extract each index (https://blog.xenoscr.net/resources/images/2021-09-06-Exploring-Virtual-Memory-and-Page-Structures/image-20210831220831378.png)
    uint64_t pml4_index = (virtual_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virtual_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virtual_addr >> 12) & 0x1FF;

    uint64_t *pdpt = get_page_table_index(pml4, pml4_index);
    uint64_t *pd   = get_page_table_index(pdpt, pdpt_index);
    uint64_t *pt   = get_page_table_index(pd, pd_index);

    // put phys addr along with flags
    pt[pt_index] = (physical_addr & ~0xFFFull) | flags;

    // flush TBL since we just invalidated whatever was at that address
    flush_tlb(virtual_addr);
}

void VMM::unmap_page(uint64_t *pml4, uint64_t virtual_addr) {
    if (pml4 == nullptr)
        pml4 = VMM::get_pml4();

    uint64_t pml4_i = (virtual_addr >> 39) & 0x1FF;
    uint64_t pdpt_i = (virtual_addr >> 30) & 0x1FF;
    uint64_t pd_i   = (virtual_addr >> 21) & 0x1FF;
    uint64_t pt_i   = (virtual_addr >> 12) & 0x1FF;

    uint64_t *pdpt = peek_table(pml4, pml4_i);
    if (pdpt == nullptr)
        return;

    uint64_t *pd = peek_table(pdpt, pdpt_i);
    if (pd == nullptr)
        return;

    uint64_t *pt = peek_table(pd, pd_i);
    if (pt == nullptr)
        return;

    pt[pt_i] = 0;

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

// checks if one 4KB page is valid 
// do not call this function from inside the kernel
// true -> valid
// false -> invalid
static bool is_user_page_ok(uint64_t vaddr, bool require_write) {
    uint64_t pml4_i = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_i = (vaddr >> 30) & 0x1FF;
    uint64_t pd_i   = (vaddr >> 21) & 0x1FF;
    uint64_t pt_i   = (vaddr >> 12) & 0x1FF;

    // caller's own CR3 since syscalls run in the caller's addr space
    uint64_t *pml4 = VMM::get_pml4();
    // start checking tables
    if ((pml4[pml4_i] & PTE_PRESENT) == 0 || (pml4[pml4_i] & PTE_USER) == 0) 
        return false;

    uint64_t *pdpt = peek_table(pml4, pml4_i);
    if (!pdpt || (pdpt[pdpt_i] & PTE_PRESENT) == 0 || (pdpt[pdpt_i] & PTE_USER) == 0) 
        return false;

    uint64_t *pd = peek_table(pdpt, pdpt_i);
    if (!pd || (pd[pd_i] & PTE_PRESENT) == 0 || (pd[pd_i] & PTE_USER) == 0) 
        return false;

    uint64_t *pt = peek_table(pd, pd_i);
    if (!pt) return false;

    uint64_t pte = pt[pt_i];
    if ((pte & PTE_PRESENT) == 0 || (pte & PTE_USER) == 0) 
        return false;
    if (require_write && (pte & PTE_READ_WRITE) == 0)      
        return false;

    return true;
}

bool VMM::validate_userland_memory(void *address, size_t length, bool require_write) {
    uint64_t start = reinterpret_cast<uint64_t>(address);

    if (address == nullptr || length == 0)
        return false;
    if (start >= USER_SPACE_TOP)
        return false;
    if (start + length < start) // check for overflow   
        return false;
    if (start + length > USER_SPACE_TOP) // check if it spills into kernel's half
        return false;

    // round down start to the nearest page
    uint64_t page = start & ~(FRAME_SIZE - 1);
    uint64_t end  = start + length;

    for (; page < end; page += FRAME_SIZE) {
        if (!is_user_page_ok(page, require_write))
            return false;
    }
    return true;
}
