#pragma once
#include <stdint.h>

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp[3];       // Privilege Stack Table (RSP0, RSP1, RSP2)
    uint64_t reserved1;
    uint64_t ist[7];       // Interrupt Stack Table (IST1 through IST7)
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;   // I/O Map Base Address
} __attribute__((packed));

// initializes tss
void tss_init();
