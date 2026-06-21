#pragma once
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;     // Lower 16 bits of the limit
    uint16_t base_low;      // Lower 16 bits of the base
    uint8_t  base_middle;   // Next 8 bits of the base
    uint8_t  access;        // Access flags (Ring level, executable, etc.)
    uint8_t  granularity;   // Granularity and Long Mode flags + limit high
    uint8_t  base_high;     // Last 8 bits of the base
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;         // Size of the entire GDT minus 1
    uint64_t base;          // The memory address of the first GDT entry
} __attribute__((packed));

// inits gdt table and loads it
void handle_gdt();
