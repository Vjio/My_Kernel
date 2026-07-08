#pragma once
#include <cstdint>
#include <cstddef>

#define USED        0
#define FREE        1
#define INIT_SIZE   8192

// TODO: later optimization, find a way to get rid of
// the extra mem overhead
struct heap_node {
    struct heap_node *next;
    struct heap_node *prev;
    uint32_t size;
    uint8_t status;
    uint8_t unused[3];
};

void *malloc(size_t size);
void *calloc(size_t num, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void heap_init(uint64_t hhdm_offset);
void heap_dump_stats();
