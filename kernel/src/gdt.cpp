#include "gdt.hpp"
#include "interrupts/tss.hpp"
#include <cstdint>

// create gdt table of 5 entries (NULL, kernel code, kernel data, and the TSS split into 2)
struct gdt_entry gdt[5];
struct gdt_ptr gdtr;

extern struct tss_entry tss;

static void gdt_set_gate(int num, uint8_t access, uint8_t gran) {
    gdt[num].base_low = 0;
    gdt[num].base_middle = 0;
    gdt[num].base_high = 0;
    gdt[num].limit_low = 0;
    
    gdt[num].access = access;
    gdt[num].granularity = gran;
}

static void gdt_set_tss(int num, uint64_t base, uint32_t limit) {
    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    
    // 0x89 means: Present(1), Ring 0(00), 64-bit TSS(1001)
    gdt[num].access      = 0x89; 
    
    gdt[num].granularity = ((limit >> 16) & 0x0F);
    gdt[num].base_high   = (base >> 24) & 0xFF;

    // We treat the next 8 bytes as two 32-bit integers to write the high address
    uint32_t* next_entry = reinterpret_cast<uint32_t*>(&gdt[num + 1]);
    next_entry[0] = (base >> 32) & 0xFFFFFFFF; // Upper 32 bits of the base
    next_entry[1] = 0;                         // Top 32 bits are reserved by CPU
}

extern "C" void load_gdt(uint64_t pointer_address);

void setup_gdt() {
    // NULL entry
    gdt_set_gate(0, 0, 0);

    // kernel code 
    // Access 0x9A: Present(1), Ring0(00), System(1), Executable(1), Read(1)
    // Gran   0xAF: Page-Granularity(1), 64-bit Long Mode(1), Limit High(1111)
    gdt_set_gate(1, 0x9A, 0xAF);

    // kernel data segment
    // Access 0x92: Present(1), Ring0(00), System(1), Data(0), Write(1)
    // Gran   0xAF: Page-Granularity(1), 64-bit Long Mode(1), Limit High(1111)
    gdt_set_gate(2, 0x92, 0xAF);

    uint64_t tss_base  = reinterpret_cast<uint64_t>(&tss);
    uint32_t tss_limit = sizeof(struct tss_entry) - 1;
    gdt_set_tss(3, tss_base, tss_limit);

    // set up pointer
    gdtr.limit = (sizeof(struct gdt_entry) * 5) - 1;
    gdtr.base  = reinterpret_cast<std::uint64_t>(&gdt);

    load_gdt((uint64_t)&gdtr);
}
