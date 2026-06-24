#pragma once
#include <cstdint>

extern "C" {
    // read a byte from the specified port
    uint8_t inb(uint16_t port);

    // write a byte to the specified port
    void outb(uint16_t port, uint8_t val);

    // wait for a very short hardware delay
    void io_wait(void);

    // read apic MSR
    uint64_t cpu_get_msr(uint32_t msr);

    // set apic MSR
    void cpu_set_msr(uint32_t msr, uint64_t value);
}
