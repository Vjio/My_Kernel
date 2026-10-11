#include "heap.hpp"
#include "vmm.hpp"
#include "pmm.hpp"
#include "memory.hpp"
#include "../stdio.hpp"
#include "../scheduling/process.hpp"
#include "../scheduling/scheduler.hpp"
#include <stdint.h>
#include <stdbool.h>

#define MIN_ALLOC_SIZE  16

// first node in double linked list
static struct heap_node *heap_start = nullptr;
static spinlock heap_lock;
static uint64_t heap_end;

void heap_init(uint64_t hhdm_offset, struct process *proc) {
    // heap_init runs in kernel land, thus we can just call VMM and
    // not go through the lengthier process of a syscall
    if (proc->is_kernel_process) {
        if (!VMM::map_pages(nullptr, KERNEL_HEAP_BASE, INIT_SIZE / FRAME_SIZE, PTE_PRESENT | PTE_READ_WRITE)) {
            // i cant think of any reason this should happen
            printf("thread heap init failed!\n");
            while(true) {;}
        }
        heap_start = reinterpret_cast<struct heap_node *> (KERNEL_HEAP_BASE);
        proc->heap_end = reinterpret_cast<uint64_t>(heap_start) + INIT_SIZE;
        heap_end = proc->heap_end;

    } else {
        // for userland processes, the heap is init on the first brk call
        proc->heap_end = HEAP_BASE;
        return;
    }

    heap_start->next = nullptr;
    heap_start->prev = nullptr;
    heap_start->status = FREE;

    heap_start->size = INIT_SIZE - sizeof(struct heap_node);
    heap_lock.locked = false;
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

// set merge to false if you don't want the last node to merge with the newly made node
// set merge to true if you want the last node to merge with the newly made node
static void expand_heap(struct heap_node *last_node, size_t requested_size, bool merge) {
    if (!merge)
        requested_size += sizeof(struct heap_node);
    // force a round up
    int pages_to_alloc = (requested_size + FRAME_SIZE - 1) / FRAME_SIZE;

    uint64_t old_heap_end = heap_end;

    if (!VMM::map_pages(nullptr, heap_end, pages_to_alloc, PTE_PRESENT | PTE_READ_WRITE)) {
        // TODO: handle running out of memory gracefully
        printf("heap expansion failed!\n");
        while(true) {;}
    }
    heap_end += pages_to_alloc * FRAME_SIZE;

    struct heap_node *new_node;
    size_t offset = 0;
    if (merge == true) {
        // expand current node
        last_node->size += requested_size;

        // check if requested size is exactly how much we allocated
        // or if if we alloced just a bit over the requested size
        if (requested_size == pages_to_alloc * FRAME_SIZE ||
            requested_size + MIN_ALLOC_SIZE + sizeof(struct heap_node) >= pages_to_alloc * FRAME_SIZE) {
            last_node->size += pages_to_alloc * FRAME_SIZE - requested_size;
            return;
        }

        // there was extra space allocated, chop it into a new node
        new_node = reinterpret_cast<struct heap_node *> (old_heap_end + requested_size);
        offset = requested_size;
    } else {
        // make a new node with all of the space
        new_node = reinterpret_cast<struct heap_node *> (old_heap_end);
    }
    // make new node
    new_node->next = nullptr;
    new_node->prev = last_node;
    new_node->size = (pages_to_alloc * FRAME_SIZE) - offset - sizeof(struct heap_node);
    new_node->status = FREE;

    last_node->next = new_node;
}

void *malloc(size_t size) {
    if (size < MIN_ALLOC_SIZE)
        size = MIN_ALLOC_SIZE;

    acquire(&heap_lock);
    // find first block in heap with at least size space available
    struct heap_node *temp = heap_start;
    struct heap_node *prev = nullptr;
    while(temp != nullptr) {
        if (temp->size >= size && temp->status == FREE)
            break;
        prev = temp;
        temp = temp->next;
    }
    // check if first node passed checks
    if (prev == nullptr)
        prev = heap_start;

    // found a node that is big enough
    if (temp != nullptr) {
        if (temp->size >= size + sizeof(struct heap_node) + MIN_ALLOC_SIZE) {
            split_node(temp, size);
        } else {
            // If we can't cleanly split, just give them the slightly larger block
            temp->status = USED;
        }

        release(&heap_lock);
        return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(temp)
            + sizeof(struct heap_node));
    }

    // heap has no node large enough for request
    if (prev->status == FREE) {
        // increase size of current node and return it
        expand_heap(prev, size, true);
        prev->status = USED;
        release(&heap_lock);
        return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(prev)
            + sizeof(struct heap_node));
    }

    // make a new node and return it
    expand_heap(prev, size, false);
    prev->next->status = USED;
    release(&heap_lock);
    return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(prev->next)
            + sizeof(struct heap_node));
}

void *zalloc(size_t size) {
    void *ptr = malloc(size);
    
    if (ptr != nullptr)
        memset(ptr, 0, size);
    
    return ptr;
}

void *calloc(size_t num, size_t size) {
    if (num != 0 && size > SIZE_MAX / num)
        return nullptr;

    size_t total_size = num * size;
    return zalloc(total_size);
}

// always call this AFTER merge_with_right
// tries to merge given node with node to its left
static void try_merge_with_left(struct heap_node *node) {
    if (node->prev != nullptr && node->prev->status == FREE) {
        node->prev->size = node->prev->size + node->size + sizeof(struct heap_node);
        
        // update pointers
        node->prev->next = node->next;
        if (node->next != nullptr) {
            node->next->prev = node->prev;
        }
    }
}

// always call this BEFORE merge_with_left
// tries to merge given node with node to its right
static void try_merge_with_right(struct heap_node *node) {
    if (node->next != nullptr && node->next->status == FREE) {
        node->size = node->size + node->next->size + sizeof(struct heap_node);
        
        // update pointers
        node->next = node->next->next;
        if (node->next != nullptr) {
            node->next->prev = node;
        }
    }
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
    try_merge_with_right(node);

    // try to merge current node with the node to its left
    try_merge_with_left(node);
}

void *realloc(void *ptr, size_t size) {
    if (ptr == nullptr)
        return malloc(size);

    // extract the header
    struct heap_node *node = reinterpret_cast<struct heap_node *>(
        reinterpret_cast<uint8_t *>(ptr) - sizeof(struct heap_node));

    // edge case checks
    if (size == 0) {
        free(ptr);
        return nullptr;
    }

    if (size < MIN_ALLOC_SIZE)
        size = MIN_ALLOC_SIZE;

    if (node->size == size)
        return ptr;
        
    acquire(&heap_lock);
    // see if we can just downsize node
    if (node->size >= size + sizeof(struct heap_node) + MIN_ALLOC_SIZE) {
        struct heap_node *new_node = reinterpret_cast<struct heap_node *>(
            reinterpret_cast<uint8_t *>(ptr) + size);
        new_node->next = node->next;
        new_node->prev = node;
        new_node->size = node->size - size - sizeof(struct heap_node);
        new_node->status = FREE;

        if (node->next != nullptr)
            node->next->prev = new_node;

        node->next = new_node;
        node->size = size;

        // try to merge new node
        try_merge_with_right(new_node);

        release(&heap_lock);
        return ptr;
    }

    // try to just expand into next node
    if (node->next != nullptr && node->next->status == FREE) {
        size_t combined_size = node->size + sizeof(struct heap_node) + node->next->size;

        if (combined_size >= size) {
            node->size = combined_size;
            node->next = node->next->next;
            if (node->next != nullptr) {
                node->next->prev = node;
            }

            // check if there is extra space to split
            if (node->size >= size + sizeof(struct heap_node) + MIN_ALLOC_SIZE)
                split_node(node, size);
            release(&heap_lock);
            return ptr;
        }
    }

    release(&heap_lock);
    // ask for a new node
    void *new_ptr = malloc(size);
    if (new_ptr == nullptr) {
        // this should not happen. should change this to a more robust log
        printf("critical failure in realloc\n");
        return nullptr; 
    }

    memcpy(new_ptr, ptr, node->size < size ? node->size : size);
    free(ptr);

    return new_ptr;
}
