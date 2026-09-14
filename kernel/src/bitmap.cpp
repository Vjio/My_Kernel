#include "bitmap.hpp"

// internal bitmap manipulation
// bit = bit_pos % 8
// index = bit_pos / 8

void set_bit(uint8_t *bitmap, uint64_t bit_pos) {
    bitmap[bit_pos / 8] |= (1 << (bit_pos % 8));
}
void clear_bit(uint8_t *bitmap, uint64_t bit_pos) {
    bitmap[bit_pos / 8] &= ~(1 << (bit_pos % 8));
}

bool test_bit(uint8_t *bitmap, uint64_t bit_pos) {
    return bitmap[bit_pos / 8] & (1 << (bit_pos % 8));
}

uint64_t set_first_free_zero(uint8_t *bitmap, uint64_t max_bytes) {
    uint64_t bit_pos = find_first_zero(bitmap, max_bytes);
    return bit_pos;
}

uint64_t find_first_zero(uint8_t *bitmap, uint64_t max_bytes) {
    for (uint64_t cur_pos = 0; cur_pos < max_bytes; cur_pos++) {
        for (uint8_t i = 0; i < 8; i++) {
            if (!test_bit(bitmap, cur_pos * 8 + i)) {
                return cur_pos * 8 + i;
            }
        }
    }

    return UINT64_MAX;
}
