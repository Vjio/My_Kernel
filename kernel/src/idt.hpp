#pragma once
#include <stdint.h>

struct idt_entry {
    // IDT -> interrrupt descritor table
    // ISR -> interrupt serive routine (the handler)
    // IST -> Interrupt stack table
    uint16_t    isr_low;      // The lower 16 bits of the ISR's address
    uint16_t    kernel_cs;    // The GDT segment selector that the CPU will load into CS before calling the ISR
    uint8_t	    ist;          // The IST in the TSS that the CPU will load into RSP; set to zero for most entries
    uint8_t     attributes;   // Type and attributes; see the IDT page
    uint16_t    isr_mid;      // The higher 16 bits of the lower 32 bits of the ISR's address
    uint32_t    isr_high;     // The higher 32 bits of the ISR's address
    uint32_t    reserved;     // Set to zero
} __attribute__((packed));

struct idtr {
	uint16_t	limit;
	uint64_t	base;
} __attribute__((packed));

// i tried to model the x86 interrupt stack frame
// so as to have an easier time debuggind later.
// if needed, this can be extended to hold some of the usual registers too
struct interrupt_frame {
    // pushed by the individual stubs
    uint64_t int_no;
    uint64_t error_code;

    // pushed automatically by the CPU on interrupt
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

// forward declaration of stubs
extern "C" void isr_stub_0();  extern "C" void isr_stub_1();
extern "C" void isr_stub_2();  extern "C" void isr_stub_3();
extern "C" void isr_stub_4();  extern "C" void isr_stub_5();
extern "C" void isr_stub_6();  extern "C" void isr_stub_7();
extern "C" void isr_stub_8();  extern "C" void isr_stub_9();
extern "C" void isr_stub_10(); extern "C" void isr_stub_11();
extern "C" void isr_stub_12(); extern "C" void isr_stub_13();
extern "C" void isr_stub_14(); extern "C" void isr_stub_15();
extern "C" void isr_stub_16(); extern "C" void isr_stub_17();
extern "C" void isr_stub_18(); extern "C" void isr_stub_19();
extern "C" void isr_stub_20(); extern "C" void isr_stub_21();
extern "C" void isr_stub_22(); extern "C" void isr_stub_23();
extern "C" void isr_stub_24(); extern "C" void isr_stub_25();
extern "C" void isr_stub_26(); extern "C" void isr_stub_27();
extern "C" void isr_stub_28(); extern "C" void isr_stub_29();
extern "C" void isr_stub_30(); extern "C" void isr_stub_31();

extern "C" void load_idt(uint64_t ptr);

// inits idt table and loads it
void setup_idt();
