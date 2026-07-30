#pragma once
#include <stdint.h>

#define GDT_KERNEL_CODE (0x08 | 0)
#define GDT_KERNEL_DATA (0x10 | 0)
#define GDT_USER_DATA   (0x28 | 3)
#define GDT_USER_CODE   (0x30 | 3)

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
void setup_gdt();
