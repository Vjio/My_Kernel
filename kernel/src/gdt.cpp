#include "gdt.hpp"
#include <cstdint>
// create gdt table of 3 entries (NULL, kernel code, kernel data)
struct gdt_entry gdt[3];
struct gdt_ptr gdtr;

static void gdt_set_gate(int num, uint8_t access, uint8_t gran) {
    gdt[num].base_low = 0;
    gdt[num].base_middle = 0;
    gdt[num].base_high = 0;
    gdt[num].limit_low = 0;
    
    gdt[num].access = access;
    gdt[num].granularity = gran;
}

extern "C" void load_gdt(uint64_t pointer_address);

void handle_gdt() {
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

    // set up pointer
    gdtr.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gdtr.base  = reinterpret_cast<std::uint64_t>(&gdt);;

    load_gdt((uint64_t)&gdtr);
}
