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

void heap_init(uint64_t hhdm_offset, struct process *proc) {
    // heap_init runs in kernel land, thus we can just call VMM and
    // not go through the lengthier process of a syscall
    if (proc->is_kernel_process) {
        heap_start = reinterpret_cast<struct heap_node *>
            (PMM::alloc_frames(INIT_SIZE / FRAME_SIZE) + hhdm_offset);
        if (heap_start == 0) {
            // TODO: rewrite heap to gracefully handle running out of memory
            // sorry future me for technical debt
            printf("thread heap init failed!\n");
            while(true) {;}
        }
        proc->heap_end = reinterpret_cast<uint64_t>(heap_start) + INIT_SIZE;

    } else {
        if (!VMM::map_pages(reinterpret_cast<uint64_t *>(proc->root_page_table), HEAP_BASE,
            INIT_SIZE / FRAME_SIZE, PTE_PRESENT | PTE_READ_WRITE | PTE_USER)) {
                // TODO: rewrite heap to gracefully handle running out of memory
                // sorry future me for technical debt
                printf("thread heap init failed!\n");
                while(true) {;}
        }
        heap_start = reinterpret_cast<struct heap_node *>(HEAP_BASE);
        proc->heap_end = HEAP_BASE + INIT_SIZE;
    }

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

// set merge to false if you don't want the last node to merge with the newly made node
// set merge to true if you want the last node to merge with the newly made node
static void expand_heap(struct heap_node *last_node, size_t requested_size, bool merge) {
    if (!merge)
        requested_size += sizeof(struct heap_node);
    // force a round up
    int pages_to_alloc = (requested_size + FRAME_SIZE - 1) / FRAME_SIZE;

    struct process *proc = Scheduler::get_current_scheduler()->get_running_thread()->parent;
    if (proc->is_kernel_process) {
        if (!VMM::map_pages(nullptr, proc->heap_end, pages_to_alloc, PTE_PRESENT | PTE_READ_WRITE | PTE_CACHE_DISABLE)) {
            // TODO: handle running out of memory gracefully
            printf("heap expansion failed!\n");
            while(true) {;}
        }
        proc->heap_end += pages_to_alloc * FRAME_SIZE;

    } else {
        // call brk
    }

    struct heap_node *new_node;
    size_t offset = 0;
    if (merge == true) {
        // expand current node
        last_node->size += requested_size;

        // check if requested size is exactly how much we allocated
        // or if if we alloced just a bit over the requested size
        if (requested_size == pages_to_alloc * FRAME_SIZE ||
            requested_size + MIN_ALLOC_SIZE + sizeof(struct heap_node) >= pages_to_alloc * FRAME_SIZE)
            return;

        // there was extra space allocated, chop it into a new node
        new_node = reinterpret_cast<struct heap_node *> (proc->heap_end + requested_size);
        offset = requested_size;
    } else {
        // make a new node with all of the space
        new_node = reinterpret_cast<struct heap_node *> (proc->heap_end);
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
    
        return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(temp)
            + sizeof(struct heap_node));
    }

    // heap has no node large enough for request
    if (prev->status == FREE) {
        // increase size of current node and return it
        expand_heap(prev, size, true);
        prev->status = USED;
        return reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(prev)
            + sizeof(struct heap_node));
    }

    // make a new node and return it
    expand_heap(prev, size, false);
    prev->next->status = USED;
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
            return ptr;
        }
    }

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
