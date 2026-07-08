#include "pmm.hpp"
#include "../stdio.hpp"

#define FRAME_USED      1
#define FRAME_UNUSED    0

// bit = frame_index % 8
// index = frame_index / 8
void PMM::set_bit(uint64_t frame_index) {
    PMM::bitmap[frame_index / 8] |= (1 << (frame_index % 8));
}

void PMM::clear_bit(uint64_t frame_index) {
    PMM::bitmap[frame_index / 8] &= ~(1 << (frame_index % 8));
}

bool PMM::test_bit(uint64_t frame_index) {
    return PMM::bitmap[frame_index / 8] & (1 << (frame_index % 8));
}

void PMM::init_PMM(struct limine_memmap_response* memmap, uint64_t hhdm_offset) {
    uint64_t highest_address = 0;
    uint64_t bitmap_physical_base = 0;
    PMM::bitmap = nullptr;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        // find the highest physical memory address in order to calculate bit map size
        uint64_t top_of_region = memmap->entries[i]->base + memmap->entries[i]->length;
        if (top_of_region > highest_address) {
            highest_address = top_of_region;
        }
    }

    PMM::total_frames = highest_address / FRAME_SIZE;
    // +7 to force a round up (so as to make sure not to omit any of the frames)
    PMM::bit_map_size = (PMM::total_frames + 7) / 8;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        if (memmap->entries[i]->type == LIMINE_MEMMAP_USABLE 
            && memmap->entries[i]->length >= PMM::bit_map_size) {
            
            bitmap_physical_base = memmap->entries[i]->base;
            PMM::bitmap = reinterpret_cast<uint8_t*>(bitmap_physical_base + hhdm_offset);
            break;
        }
    }

    if (PMM::bitmap == nullptr) {
        // no contiguous region large enough for the bitmap
        PMM::bitmap = nullptr;
        return; 
    }

    // set internal variables
    PMM::nr_free_frames = 0;
    PMM::first_free_frame = 0;

    // set all bits to used
    for (uint64_t i = 0; i < PMM::bit_map_size; i++)
        PMM::bitmap[i] = 0xFF;

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
                PMM::clear_bit(frame_index);
                PMM::nr_free_frames++;
            }
        }
    }

    // re-allocate the frames that the bitmap array occupies
    for (uint64_t offset = 0; offset < PMM::bit_map_size; offset += 4096) {
        uint64_t physical_addr = bitmap_physical_base + offset;
        uint64_t frame_index = physical_addr / 4096;

        PMM::set_bit(frame_index);
        PMM::nr_free_frames--;
    }

    // loop yet again to find the first free frame
    for (uint64_t i = 0; i < PMM::bit_map_size; i++) {
        if (PMM::bitmap[i] != 0xFF) {
            PMM::first_free_frame = i;
            break;
        }
    }
}

paddr_t PMM::alloc_frame() {
    // convention, if first_free_frame was set to a OXFF byte, that means there are no free frames
    if (PMM::bitmap[PMM::first_free_frame] == 0xFF)
        return 0;

    // find first 0 bit in byte
    uint8_t bit = 0;
    for (uint8_t i = 0; i < 8; i++) {
        uint64_t current_bit = (PMM::first_free_frame * 8) + i;
        if (!PMM::test_bit(current_bit)) {
            bit = i;
            PMM::set_bit(current_bit);
            PMM::nr_free_frames--;
            break;
        }
    }

    // compute phys address for frame
    // 8 because bitmap is uint8_t *
    paddr_t new_frame = ((8 * PMM::first_free_frame) + bit) * FRAME_SIZE;

    if (PMM::bitmap[PMM::first_free_frame] == 0xFF) {
        // byte is now fully used
        // scan for next unused bit
        uint64_t index = PMM::first_free_frame;
        for (; PMM::first_free_frame < PMM::bit_map_size; PMM::first_free_frame++) 
            if (PMM::bitmap[first_free_frame] != 0xFF)
                break;
        
        // found a byte with free bits
        if (PMM::first_free_frame < PMM::bit_map_size)
            return new_frame;

        // wrap arround and keep scanning for a free bit
        for (PMM::first_free_frame = 0; PMM::first_free_frame < index; PMM::first_free_frame++)
            if (PMM::bitmap[PMM::first_free_frame] != 0xFF)
                break;

        // if the scan succeeded, first_free_frame now points to the index
        // of a byte with free bits
        // if not, first_free_frame now points to the index of a full byte
        // in both cases, we just return the new alloced frame. nothing else to do
        return new_frame;
    }
    return new_frame;
}

paddr_t PMM::alloc_frames(uint64_t nr) {
    // convention, if first_free_frame was set to a OXFF byte, that means there are no free frames
    if (PMM::bitmap[PMM::first_free_frame] == 0xFF)
        return 0;

    if (nr > PMM::total_frames)
        return 0;

    // find first sequence of free nr frames
    uint64_t index = PMM::first_free_frame;
    uint64_t sum = 0;
    // the index and bit at which the sequence starts at
    uint64_t result_index = 0;
    uint8_t result_bit = 0;
    for (; index < PMM::bit_map_size; index++) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint64_t current_bit = (index * 8) + bit;
            if (!PMM::test_bit(current_bit)) {
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
        for (index = 0; index < PMM::first_free_frame; index++) {
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint64_t current_bit = (index * 8) + bit;
                if (!PMM::test_bit(current_bit)) {
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
        // allocation failed! this really shouldn't happen here
        printf("logically unreachalbe line in PMM!\n");
        return 0;
    }
    PMM::nr_free_frames -= nr;

    // set used bits
    index = result_index;
    uint64_t temp_bit = result_bit;
    while (nr) {
        PMM::set_bit(index * 8 + temp_bit);
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

    if (PMM::bitmap[PMM::first_free_frame] == 0xFF) {
        // byte is now fully used
        // scan for next unused bit
        uint64_t index = PMM::first_free_frame;
        for (; PMM::first_free_frame < PMM::bit_map_size; PMM::first_free_frame++) 
            if (PMM::bitmap[PMM::first_free_frame] != 0xFF)
                break;
        
        // found a byte with free bits
        if (PMM::first_free_frame < PMM::bit_map_size)
            return new_frame;

        // wrap arround and keep scanning for a free bit
        for (PMM::first_free_frame = 0; PMM::first_free_frame < index; PMM::first_free_frame++)
            if (PMM::bitmap[PMM::first_free_frame] != 0xFF)
                break;

        // if the scan succeeded, first_free_frame now points to the index
        // of a byte with free bits
        // if not, first_free_frame now points to the index of a full byte
        // in both cases, we just return the new alloced frame. nothing else to do
        return new_frame;
    }
    return new_frame;
}

void PMM::free_frame(paddr_t address) {
    uint64_t frame_index = address / FRAME_SIZE;
    uint64_t index = frame_index / 8;

    // if the bit is already 0, the frame is already free
    if (PMM::test_bit(frame_index) == 0) {
        // could add a log here for this
        // it shouldn't really happen
        return;
    }

    // free the frame
    PMM::clear_bit(frame_index);
    PMM::nr_free_frames++;

    // if we just freed a frame in a byte that is lower than our current starting search index,
    // move our starting search index back
    if (index < PMM::first_free_frame) {
        PMM::first_free_frame = index;
    }
}

void PMM::free_frames(paddr_t address, uint64_t nr) {
    uint64_t frame_index = address / FRAME_SIZE;
    uint64_t index = frame_index / 8;
    uint8_t bit = frame_index % 8;

    // if the bit is already 0, the frame is already free
    if (PMM::test_bit(frame_index) == 0) {
        // could add a log here for this
        // it shouldn't really happen
        return;
    }

    // if we just freed a frame in a byte that is lower than our current starting search index,
    // move our starting search index back
    if (index < PMM::first_free_frame) {
        PMM::first_free_frame = index;
    }

    // start freeing all the frames
    while (nr) {
        frame_index = index * 8 + bit;
        PMM::clear_bit(frame_index);
        PMM::nr_free_frames++;
        
        bit++;
        if (bit > 7) {
            bit = 0;
            index++;
        }

        nr--; 
    }
}
