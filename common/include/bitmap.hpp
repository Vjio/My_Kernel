#pragma once
#include <stdint.h>
#include <stddef.h>

void set_bit(uint8_t *bitmap, uint64_t bit_pos);
void clear_bit(uint8_t *bitmap, uint64_t bit_pos);
// returns true if bit is used
// returns false if bit is unused
bool test_bit(uint8_t *bitmap, uint64_t bit_pos);
// returns position of the first previously free bit, now set to used
// returns uint64_t max value on failure
uint64_t set_first_free_zero(uint8_t *bitmap, uint64_t max_bytes);
// returns position of first free bit
// returns uint64_t max value on failure
uint64_t find_first_zero(uint8_t *bitmap, uint64_t max_bytes);
