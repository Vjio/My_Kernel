#pragma once
#include <stdint.h>
#include <stddef.h>

// max elf binary size - 32 MB
#define MAX_ELF_SIZE    (32 * 1024 * 1024)

// where the loader reads the ELF bytes from
struct elf_source {
    void *ctx;
    // total size of the ELF image in bytes
    uint64_t size;
};

bool memory_read(struct elf_source *ctx, uint64_t offset, void *buf, uint64_t len);

// maps every PT_LOAD segment of the ELF into proc's address space (proc must NOT be
// running yet) and returns the entry point. only static ET_EXEC binaries are supported.
// failure means the program can not run and must destroy itself
bool load_elf(uint64_t *pml4, struct elf_source *src, uint64_t *entry);
