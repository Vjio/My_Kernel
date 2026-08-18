#include "pmm.hpp"
#include "../stdio.hpp"

#define FRAME_USED      1
#define FRAME_UNUSED    0

namespace {
    // internal variables
    inline static uint8_t* bitmap           = nullptr;
    inline static uint64_t bit_map_size     = 0;
    inline static uint64_t total_frames     = 0;
    inline static uint64_t nr_free_frames   = 0;
    inline static struct spinlock lock;
    // index of the first byte with at least 1 free frame (at least 1 bit set to 0)
    // convention, if first_free_frame was set to a OXFF byte, that means there are no free frames
    inline static uint64_t first_free_frame = 0;
    
    // internal bitmap manipulation
    // bit = frame_index % 8
    // index = frame_index / 8

    // set bit to used
    void set_bit(uint64_t frame_index) {
        bitmap[frame_index / 8] |= (1 << (frame_index % 8));
    }
    void clear_bit(uint64_t frame_index) {
        bitmap[frame_index / 8] &= ~(1 << (frame_index % 8));
    }
    // returns true if bit is used
    // returns false if bit is unused
    bool test_bit(uint64_t frame_index) {
        return bitmap[frame_index / 8] & (1 << (frame_index % 8));
    }
}

void PMM::init_PMM(struct limine_memmap_response* memmap, uint64_t hhdm_offset) {
    lock.locked = false;
    uint64_t highest_address = 0;
    uint64_t bitmap_physical_base = 0;
    bitmap = nullptr;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        // find the highest physical memory address in order to calculate bit map size
        uint64_t top_of_region = memmap->entries[i]->base + memmap->entries[i]->length;
        if (top_of_region > highest_address) {
            highest_address = top_of_region;
        }
    }

    total_frames = highest_address / FRAME_SIZE;
    // +7 to force a round up (so as to make sure not to omit any of the frames)
    bit_map_size = (total_frames + 7) / 8;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        if (memmap->entries[i]->type == LIMINE_MEMMAP_USABLE 
            && memmap->entries[i]->length >= bit_map_size) {
            
            bitmap_physical_base = memmap->entries[i]->base;
            bitmap = reinterpret_cast<uint8_t*>(bitmap_physical_base + hhdm_offset);
            break;
        }
    }

    if (bitmap == nullptr) {
        // no contiguous region large enough for the bitmap
        bitmap = nullptr;
        return; 
    }

    // set internal variables
    nr_free_frames = 0;
    first_free_frame = 0;

    // set all bits to used
    for (uint64_t i = 0; i < bit_map_size; i++)
        bitmap[i] = 0xFF;

    // loop through entry counts and reset every usable region to unused
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        if (memmap->entries[i]->type == LIMINE_MEMMAP_USABLE) {
            // loop through every frame in this usable region
            // usable and bootloader reclaimable entries are guaranteed by limine
            // to be 4096 byte aligned for both base and length
            for (uint64_t offset = 0; offset < memmap->entries[i]->length; offset += 4096) {
                uint64_t physical_addr = memmap->entries[i]->base + offset;
                uint64_t frame_index = physical_addr / 4096;

                // set corresponding bit to 0
                clear_bit(frame_index);
                nr_free_frames++;
            }
        }
    }

    // re-allocate the frames that the bitmap array occupies
    for (uint64_t offset = 0; offset < bit_map_size; offset += 4096) {
        uint64_t physical_addr = bitmap_physical_base + offset;
        uint64_t frame_index = physical_addr / 4096;

        set_bit(frame_index);
        nr_free_frames--;
    }

    // loop yet again to find the first free frame
    for (uint64_t i = 0; i < bit_map_size; i++) {
        if (bitmap[i] != 0xFF) {
            first_free_frame = i;
            break;
        }
    }
}

paddr_t PMM::alloc_frame() {
    acquire(&lock);
    // convention, if first_free_frame was set to a OXFF byte, that means there are no free frames
    if (bitmap[first_free_frame] == 0xFF) {
        release(&lock);
        return 0;
    }

    // find first 0 bit in byte
    uint8_t bit = 0;
    for (uint8_t i = 0; i < 8; i++) {
        uint64_t current_bit = (first_free_frame * 8) + i;
        if (!test_bit(current_bit)) {
            bit = i;
            set_bit(current_bit);
            nr_free_frames--;
            break;
        }
    }

    // compute phys address for frame
    // 8 because bitmap is uint8_t *
    paddr_t new_frame = ((8 * first_free_frame) + bit) * FRAME_SIZE;

    if (bitmap[first_free_frame] == 0xFF) {
        // byte is now fully used
        // scan for next unused bit
        uint64_t index = first_free_frame;
        for (; first_free_frame < bit_map_size; first_free_frame++) 
            if (bitmap[first_free_frame] != 0xFF)
                break;
        
        // found a byte with free bits
        if (first_free_frame < bit_map_size) {
            release(&lock);
            return new_frame;
        }

        // wrap arround and keep scanning for a free bit
        for (first_free_frame = 0; first_free_frame < index; first_free_frame++)
            if (bitmap[first_free_frame] != 0xFF)
                break;

        // if the scan succeeded, first_free_frame now points to the index
        // of a byte with free bits
        // if not, first_free_frame now points to the index of a full byte
        // in both cases, we just return the new alloced frame. nothing else to do
        release(&lock);
        return new_frame;
    }
    release(&lock);
    return new_frame;
}

paddr_t PMM::alloc_frames(uint64_t nr) {
    acquire(&lock);
    // convention, if first_free_frame was set to a OXFF byte, that means there are no free frames
    if (bitmap[first_free_frame] == 0xFF) {
        release(&lock);
        return 0;
    }

    if (nr > total_frames) {
        release(&lock);
        return 0;
    }

    // find first sequence of free nr frames
    uint64_t index = first_free_frame;
    uint64_t sum = 0;
    // the index and bit at which the sequence starts at
    uint64_t result_index = 0;
    uint8_t result_bit = 0;
    for (; index < bit_map_size; index++) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint64_t current_bit = (index * 8) + bit;
            if (!test_bit(current_bit)) {
                // current bit is unused
                if (sum == 0) {
                    // start new sum
                    result_index = index;
                    result_bit = bit;
                    sum++;
                } else {
                    // keep building previous sum
                    sum++;
                    if (sum == nr)
                        break;
                }
            } else
                sum = 0;
        }
        if (sum == nr)
            break;
    }
    if (sum != nr) {
        // couldn't find nr continous free frames in memory
        // try checking first part of bitmap
        sum = 0;
        for (index = 0; index < first_free_frame; index++) {
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint64_t current_bit = (index * 8) + bit;
                if (!test_bit(current_bit)) {
                    // current bit is unused
                    if (sum == 0) {
                        // start new sum
                        result_index = index;
                        result_bit = bit;
                        sum++;
                    } else {
                        // keep building previous sum
                        sum++;
                        if (sum == nr)
                            break;
                    }
                } else
                    sum = 0;
            }
            if (sum == nr)
                break;
        }
    }

    if (sum != nr) {
        release(&lock);
        // allocation failed! this really shouldn't happen here
        printf("logically unreachalbe line in PMM!\n");
        return 0;
    }
    nr_free_frames -= nr;

    // set used bits
    index = result_index;
    uint64_t temp_bit = result_bit;
    while (nr) {
        set_bit(index * 8 + temp_bit);
        temp_bit++;
        if (temp_bit > 7) {
            temp_bit = 0;
            index++;
        }

        nr--;
    }

    // compute phys address for first frame in sequence
    // 8 because bitmap is uint8_t *
    paddr_t new_frame = ((8 * result_index) + result_bit) * FRAME_SIZE;

    if (bitmap[first_free_frame] == 0xFF) {
        // byte is now fully used
        // scan for next unused bit
        uint64_t index = first_free_frame;
        for (; first_free_frame < bit_map_size; first_free_frame++) 
            if (bitmap[first_free_frame] != 0xFF)
                break;
        
        // found a byte with free bits
        if (first_free_frame < bit_map_size) {
            release(&lock);
            return new_frame;
        }

        // wrap arround and keep scanning for a free bit
        for (first_free_frame = 0; first_free_frame < index; first_free_frame++)
            if (bitmap[first_free_frame] != 0xFF)
                break;

        // if the scan succeeded, first_free_frame now points to the index
        // of a byte with free bits
        // if not, first_free_frame now points to the index of a full byte
        // in both cases, we just return the new alloced frame. nothing else to do
        release(&lock);
        return new_frame;
    }
    release(&lock);
    return new_frame;
}

void PMM::free_frame(paddr_t address) {
    uint64_t frame_index = address / FRAME_SIZE;
    uint64_t index = frame_index / 8;
    
    acquire(&lock);
    // if the bit is already 0, the frame is already free
    if (test_bit(frame_index) == 0) {
        release(&lock);
        // could add a log here for this
        // it shouldn't really happen
        return;
    }

    // free the frame
    clear_bit(frame_index);
    nr_free_frames++;

    // if we just freed a frame in a byte that is lower than our current starting search index,
    // move our starting search index back
    if (index < first_free_frame) {
        first_free_frame = index;
    }
    release(&lock);
}

void PMM::free_frames(paddr_t address, uint64_t nr) {
    uint64_t frame_index = address / FRAME_SIZE;
    uint64_t index = frame_index / 8;
    uint8_t bit = frame_index % 8;
    
    acquire(&lock);
    // if the bit is already 0, the frame is already free
    if (test_bit(frame_index) == 0) {
        release(&lock);
        // could add a log here for this
        // it shouldn't really happen
        return;
    }

    // if we just freed a frame in a byte that is lower than our current starting search index,
    // move our starting search index back
    if (index < first_free_frame) {
        first_free_frame = index;
    }

    // start freeing all the frames
    while (nr) {
        frame_index = index * 8 + bit;
        clear_bit(frame_index);
        nr_free_frames++;
        
        bit++;
        if (bit > 7) {
            bit = 0;
            index++;
        }

        nr--; 
    }
    release(&lock);
}
