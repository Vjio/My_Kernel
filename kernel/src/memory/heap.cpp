#include "heap.hpp"
#include "vmm.hpp"
#include "pmm.hpp"
#include "memory.hpp"
#include <stdint.h>

#define MIN_ALLOC_SIZE  16

static struct heap_node *heap_start = nullptr;

void heap_init(uint64_t hhdm_offset) {
    heap_start = (struct heap_node *)(PMM::alloc_frames(INIT_SIZE / FRAME_SIZE) 
        + hhdm_offset);

    heap_start->next = nullptr;
    heap_start->prev = nullptr;
    heap_start->status = FREE;
    
    heap_start->size = INIT_SIZE - sizeof(struct heap_node);
}

static void split_node(struct heap_node *node, size_t size) {
    // make a new node out of the current's node unused mem
    struct heap_node *new_node = reinterpret_cast<struct heap_node *>(
    reinterpret_cast<uint8_t *>(node) + sizeof(struct heap_node) + size);
    new_node->size = node->size - size - sizeof(struct heap_node);
    new_node->prev = node;
    new_node->next = node->next;
    new_node->status = FREE;

    // update next node
    if (new_node->next != nullptr)
        new_node->next->prev = new_node;

    // update current node
    node->size = size;
    node->next = new_node;
    node->status = USED;
}

void *malloc(size_t size) {
    if (size < MIN_ALLOC_SIZE)
        size = MIN_ALLOC_SIZE;

    // find first block in heap with at least size space available
    struct heap_node *temp = heap_start;
    while(temp != nullptr) {
        if (temp->size >= size && temp->status == FREE)
            break;
        temp = temp->next;
    }

    // alloc failed
    if (temp == nullptr)
        return nullptr;

    if (temp->size >= size + sizeof(struct heap_node) + MIN_ALLOC_SIZE) {
        split_node(temp, size);
    } else {
        // If we can't cleanly split, just give them the slightly larger block
        temp->status = USED;
    }

    return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(temp)
        + sizeof(struct heap_node));
}

void *zalloc(size_t size) {
    void *ptr = malloc(size);
    
    if (ptr != nullptr) {
        memset(ptr, 0, size);
    }
    
    return ptr;
}

void *calloc(size_t num, size_t size) {
    if (num != 0 && size > SIZE_MAX / num) {
        return nullptr;
    }

    size_t total_size = num * size;
    return zalloc(total_size);
}

void free(void *ptr) {
    if (ptr == nullptr)
        return;

    // extract the header
    struct heap_node *node = reinterpret_cast<struct heap_node *>(
        reinterpret_cast<uint8_t *>(ptr) - sizeof(struct heap_node));

    if (node->status == FREE) {
        // this shouldn't really happen. could add a log
        return;
    }

    node->status = FREE;

    // try to merge current node with the node to its right
    if (node->next != nullptr && node->next->status == FREE) {
        node->size = node->size + node->next->size + sizeof(struct heap_node);
        
        // update pointers
        node->next = node->next->next;
        if (node->next != nullptr) {
            node->next->prev = node;
        }
    }

    // try to merge current node with the node to its left
    if (node->prev != nullptr && node->prev->status == FREE) {
        node->prev->size = node->prev->size + node->size + sizeof(struct heap_node);
        
        // update pointers
        node->prev->next = node->next;
        if (node->next != nullptr) {
            node->next->prev = node->prev;
        }
    }
}
