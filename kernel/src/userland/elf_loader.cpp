#include "elf_loader.hpp"
#include "elf.hpp"
#include "../memory/vmm.hpp"
#include "../memory/pmm.hpp"
#include "../memory/memory.hpp"
#include "../stdio.hpp"
#include "../scheduling/process.hpp"

// segments must sit inside this window of the user half:
// not the null page, and below the user heap (the stacks live above it)
#define USER_LOAD_MIN   FRAME_SIZE
#define USER_LOAD_MAX   HEAP_BASE

// memcpy wrapper that returns false if it tries to read out of the file
bool memory_read(struct elf_source *img, uint64_t offset, void *buf, uint64_t len) {
    if (offset > img->size || len > img->size - offset)
        return false;

    memcpy(buf, reinterpret_cast<uint8_t *>(img->ctx) + offset, len);
    return true;
}

static bool validate_header(const struct elf64_ehdr *eh, uint64_t file_size) {
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        printf("elf: bad magic\n");
        return false;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_ident[EI_DATA] != ELFDATA2LSB) {
        printf("elf: not a little endian 64 bit ELF\n");
        return false;
    }
    if (eh->e_type != ET_EXEC) {
        printf("elf: only static ET_EXEC binaries are supported\n");
        return false;
    }
    if (eh->e_machine != EM_X86_64) {
        printf("elf: not an x86_64 binary\n");
        return false;
    }
    if (eh->e_phentsize != sizeof(struct elf64_phdr) || eh->e_phnum == 0) {
        printf("elf: bad program header table\n");
        return false;
    }
    // the whole program header table has to be inside the file
    uint64_t table_size = static_cast<uint64_t>(eh->e_phnum) * eh->e_phentsize;
    if (eh->e_phoff > file_size || table_size > file_size - eh->e_phoff) {
        printf("elf: program headers are outside the file\n");
        return false;
    }
    return true;
}

// checks that a PT_LOAD segment is correct and fits the address window
static bool validate_segment(struct elf64_phdr *ph, uint64_t file_size) {
    if (ph->p_filesz > ph->p_memsz)
        return false;
    // bytes in the file
    if (ph->p_offset > file_size || ph->p_filesz > file_size - ph->p_offset)
        return false;
    // check if it even fits the window
    if (ph->p_vaddr < USER_LOAD_MIN || ph->p_vaddr >= USER_LOAD_MAX)
        return false;
    // vaddr is below USER_LOAD_MAX here so this subtraction can't underflow
    if (ph->p_memsz > USER_LOAD_MAX - ph->p_vaddr)
        return false;
    return true;
}

// asks the VMM for a zeroed frame at vaddr
// fills the part of it that the file covers
static bool load_page(uint64_t *pml4, struct elf_source *src, struct elf64_phdr *ph,
    uint64_t vaddr, uint64_t flags) {
    uint8_t *page = VMM::map_zeroed_page(pml4, vaddr, flags);
    if (page == nullptr)
        return false;

    // only read the page if it falls in [p_vaddr, p_vaddr + filesz]
    uint64_t file_start = ph->p_vaddr;
    uint64_t file_end = ph->p_vaddr + ph->p_filesz;
    uint64_t low = vaddr > file_start ? vaddr : file_start;
    uint64_t high = (vaddr + FRAME_SIZE) < file_end ? (vaddr + FRAME_SIZE) : file_end;

    return low >= high ||
        memory_read(src, ph->p_offset + (low - file_start), page + (low - vaddr), high - low);
}

bool load_elf(uint64_t *pml4, struct elf_source *src, uint64_t *entry) {
    struct elf64_ehdr eh;
    if (src->size < sizeof(eh) || !memory_read(src, 0, &eh, sizeof(eh))) {
        printf("elf: can't read the header\n");
        return false;
    }

    if (!validate_header(&eh, src->size))
        return false;

    // first page that no earlier segment has touched. PT_LOAD entries must come in
    // ascending order and never share a page, since map_page would silently replace
    // the first mapping
    uint64_t next_free_va = USER_LOAD_MIN;
    bool entry_in_code = false;

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        struct elf64_phdr ph;
        if (!memory_read(src, eh.e_phoff + static_cast<uint64_t>(i) * eh.e_phentsize,
                &ph, sizeof(ph))) {
            printf("elf: can't read program header %d\n", i);
            return false;
        }

        if (ph.p_type != PT_LOAD || ph.p_memsz == 0)
            continue;

        if (!validate_segment(&ph, src->size)) {
            printf("elf: segment %d is invalid\n", i);
            return false;
        }

        uint64_t start = ph.p_vaddr & ~(FRAME_SIZE - 1ULL);
        uint64_t end = (ph.p_vaddr + ph.p_memsz + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1ULL);
        if (start < next_free_va) {
            printf("elf: segment %d overlaps or is out of order\n", i);
            return false;
        }

        // no NX support yet, so every mapped page is executable whatever the flags say
        uint64_t flags = PTE_PRESENT | PTE_USER | ((ph.p_flags & PF_W) ? PTE_READ_WRITE : 0);
        for (uint64_t vaddr = start; vaddr < end; vaddr += FRAME_SIZE) {
            if (!load_page(pml4, src, &ph, vaddr, flags)) {
                printf("elf: failed loading the page at %lx\n", vaddr);
                return false;
            }
        }
        next_free_va = end;

        if ((ph.p_flags & PF_X) && eh.e_entry >= ph.p_vaddr && eh.e_entry < ph.p_vaddr + ph.p_memsz)
            entry_in_code = true;
    }

    if (!entry_in_code) {
        printf("elf: entry point is not inside an executable segment\n");
        return false;
    }

    *entry = eh.e_entry;
    return true;
}
