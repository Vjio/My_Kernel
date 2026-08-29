#include "pmm.hpp"
#include "vmm.hpp"
#include "memory.hpp"

uint64_t l_hhdm_offset    = 0;
uint64_t kernel_pml4_phys = 0;

extern "C" uint64_t get_cr3();
extern "C" void flush_tlb(uint64_t addr);

static constexpr uint64_t USER_SPACE_TOP = 0x0000800000000000ull;

struct page_indices {
    uint64_t pml4_i, pdpt_i, pd_i, pt_i;
};

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

static page_indices get_indices(uint64_t vaddr) {
    return {
        (vaddr >> 39) & 0x1FF,
        (vaddr >> 30) & 0x1FF,
        (vaddr >> 21) & 0x1FF,
        (vaddr >> 12) & 0x1FF,
    };
}

// returns the address of the next table or nullptr if it doesn't exist
static uint64_t *peek_table(uint64_t *table, uint64_t index) {
    if ((table[index] & PTE_PRESENT) == 0)
        return nullptr;
    uint64_t next_phys = table[index] & ~0xFFFull;
    return reinterpret_cast<uint64_t *>(next_phys + l_hhdm_offset);
}

// walks pml4 -> pdpt -> pd -> pt (no allocation)
// returns nullptr if any level along the way is not present
// does not check pt itself
static uint64_t *walk_to_pt(uint64_t *pml4, struct page_indices idx) {
    uint64_t *pdpt = peek_table(pml4, idx.pml4_i);
    if (!pdpt) 
        return nullptr;
    uint64_t *pd = peek_table(pdpt, idx.pdpt_i);
    if (!pd) 
        return nullptr;
    return peek_table(pd, idx.pd_i);
}

uint64_t *VMM::get_page_table_index(uint64_t* table, uint64_t index) {
    if ((table[index] & PTE_PRESENT) == 0) {
        // index hasn't been allocated. ask pmm for a new frame
        uint64_t new_table_phys = PMM::alloc_frame();
        if (new_table_phys == 0)
            return nullptr;

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

bool VMM::map_page(uint64_t *pml4, uint64_t virtual_addr, uint64_t physical_addr,
    uint64_t flags) {
    if (pml4 == nullptr)
        pml4 = VMM::get_pml4();
    else 
        pml4 = reinterpret_cast<uint64_t*>(reinterpret_cast<uint64_t>(pml4) + l_hhdm_offset);

    bool allocated_frame = false;
    if (physical_addr == 0) {
        physical_addr = PMM::alloc_frame();
        if (physical_addr == 0)
            return false;
        allocated_frame = true;
    }

    // extract each index (https://blog.xenoscr.net/resources/images/2021-09-06-Exploring-Virtual-Memory-and-Page-Structures/image-20210831220831378.png)
    struct page_indices idx = get_indices(virtual_addr);

    uint64_t *pdpt = get_page_table_index(pml4, idx.pml4_i);
    uint64_t *pd   = pdpt ? get_page_table_index(pdpt, idx.pdpt_i) : nullptr;
    uint64_t *pt   = pd   ? get_page_table_index(pd, idx.pd_i)     : nullptr;

    if (pt == nullptr) {
        if (allocated_frame)
            PMM::free_frame(physical_addr);
        return false;
    }

    // put phys addr along with flags
    pt[idx.pt_i] = (physical_addr & ~0xFFFull) | flags;

    // flush TBL since we just invalidated whatever was at that address
    flush_tlb(virtual_addr);
    return true;
}

bool VMM::map_pages(uint64_t *pml4, uint64_t virtual_addr, uint64_t nr_of_pages, uint64_t flags) {
    for (uint64_t i = 0; i < nr_of_pages; i++) {
        if (!VMM::map_page(pml4, virtual_addr + i * FRAME_SIZE, 0, flags)) {
            for (uint64_t j = 0; j < i; j++)
                VMM::unmap_and_free_page(pml4, virtual_addr + j * FRAME_SIZE);
            return false;
        }
    }
    return true;
}

// walks to the PT, clears the entry, and returns the physical frame 
// that was mapped there (0 if none)
static uint64_t clear_pte_and_get_phys(uint64_t *pml4, uint64_t virtual_addr) {
    struct page_indices idx = get_indices(virtual_addr);

    uint64_t *pt = walk_to_pt(pml4, idx);
    if (pt == nullptr) return 0;

    uint64_t phys = pt[idx.pt_i] & ~0xFFFull;
    pt[idx.pt_i] = 0;
    flush_tlb(virtual_addr);
    return phys;
}

void VMM::unmap_page(uint64_t *pml4, uint64_t virtual_addr) {
    if (pml4 == nullptr)
        pml4 = VMM::get_pml4();
    clear_pte_and_get_phys(pml4, virtual_addr);
}

void VMM::unmap_and_free_page(uint64_t *pml4, uint64_t virtual_addr) {
    if (pml4 == nullptr) 
        pml4 = VMM::get_pml4();

    uint64_t phys = clear_pte_and_get_phys(pml4, virtual_addr);
    if (phys != 0)
        PMM::free_frame(phys);
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
    struct page_indices idx = get_indices(vaddr);

    // caller's own CR3 since syscalls run in the caller's addr space
    uint64_t *pml4 = VMM::get_pml4();
    // start checking tables
    if ((pml4[idx.pml4_i] & PTE_PRESENT) == 0 || (pml4[idx.pml4_i] & PTE_USER) == 0) 
        return false;

    uint64_t *pdpt = peek_table(pml4, idx.pml4_i);
    if (!pdpt || (pdpt[idx.pdpt_i] & PTE_PRESENT) == 0 || (pdpt[idx.pdpt_i] & PTE_USER) == 0) 
        return false;

    uint64_t *pd = peek_table(pdpt, idx.pdpt_i);
    if (!pd || (pd[idx.pd_i] & PTE_PRESENT) == 0 || (pd[idx.pd_i] & PTE_USER) == 0) 
        return false;

    uint64_t *pt = peek_table(pd, idx.pd_i);
    if (!pt) 
        return false;

    uint64_t pte = pt[idx.pt_i];
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

uint64_t VMM::get_physical_address(void *addr) {
    uint64_t virtual_addr = reinterpret_cast<uint64_t>(addr);
    struct page_indices idx = get_indices(virtual_addr);
    uint64_t offset = virtual_addr & 0xFFF; 

    uint64_t *pml4 = VMM::get_pml4();
    if (pml4 == nullptr)
        return 0;

    uint64_t *pt = walk_to_pt(pml4, idx);
    if (pt == nullptr) 
        return 0;

    uint64_t pte = pt[idx.pt_i];
    if ((pte & PTE_PRESENT) == 0) 
        return 0;

    // mask out the flags in order to get the actual frame
    uint64_t phys_frame = pte & ~0xFFFull;
    return phys_frame + offset;
}
