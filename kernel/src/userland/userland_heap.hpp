#pragma once
#include <cstdint>
#include <cstddef>

#define FRAME_SIZE  4096
#define USED        0
#define FREE        1
#define INIT_SIZE   FRAME_SIZE * 4

// TODO: later optimization, find a way to get rid of
// the extra mem overhead
struct heap_node {
    struct heap_node *next;
    struct heap_node *prev;
    uint32_t size;
    uint8_t status;
    uint8_t unused[3];
};

// TODO: get rid of this after writing ELF loader and file system
// this is just a bandaid solution so everything can compile into the kernel image
namespace userland {
    void *malloc(size_t size);
    void *calloc(size_t num, size_t size);
    void *realloc(void *ptr, size_t size);
    void *zalloc(size_t size);
    void free(void *ptr);
    void heap_dump_stats();
}
