#include "idt.hpp"
#include "stdio.hpp"

// you will notice a lot of "manually" written large arrays of data.
// there were smarter ways to do this. i just asked ai to generate them for me
// since i will never have to change this code (thus it does not need to be scalable)
static void* isr_stub_table[32] = {
    (void*)isr_stub_0,  (void*)isr_stub_1,  (void*)isr_stub_2,  (void*)isr_stub_3,
    (void*)isr_stub_4,  (void*)isr_stub_5,  (void*)isr_stub_6,  (void*)isr_stub_7,
    (void*)isr_stub_8,  (void*)isr_stub_9,  (void*)isr_stub_10, (void*)isr_stub_11,
    (void*)isr_stub_12, (void*)isr_stub_13, (void*)isr_stub_14, (void*)isr_stub_15,
    (void*)isr_stub_16, (void*)isr_stub_17, (void*)isr_stub_18, (void*)isr_stub_19,
    (void*)isr_stub_20, (void*)isr_stub_21, (void*)isr_stub_22, (void*)isr_stub_23,
    (void*)isr_stub_24, (void*)isr_stub_25, (void*)isr_stub_26, (void*)isr_stub_27,
    (void*)isr_stub_28, (void*)isr_stub_29, (void*)isr_stub_30, (void*)isr_stub_31,
};

// Exception name table for readable output
static const char* exception_names[32] = {
    "Divide by Zero",               // 0
    "Debug",                        // 1
    "Non-Maskable Interrupt",       // 2
    "Breakpoint",                   // 3
    "Overflow",                     // 4
    "Bound Range Exceeded",         // 5
    "Invalid Opcode",               // 6
    "Device Not Available",         // 7
    "Double Fault",                 // 8
    "Coprocessor Segment Overrun",  // 9
    "Invalid TSS",                  // 10
    "Segment Not Present",          // 11
    "Stack-Segment Fault",          // 12
    "General Protection Fault",     // 13
    "Page Fault",                   // 14
    "Reserved",                     // 15
    "x87 FPU Error",                // 16
    "Alignment Check",              // 17
    "Machine Check",                // 18
    "SIMD Floating-Point",          // 19
    "Virtualization",               // 20
    "Control Protection",           // 21
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved",
    "Hypervisor Injection",         // 28
    "VMM Communication",            // 29
    "Security Exception",           // 30
    "Reserved",                     // 31
};

#define COM1_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %0, %1" : : "a"(data), "Nd"(port));
}
static void serial_print(const char* s) {
    while (*s) {
        outb(COM1_PORT, *s++);
    }
}

static void serial_print_hex(uint64_t val) {
    const char* hex = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) {
        outb(COM1_PORT, hex[(val >> i) & 0xF]);
    }
}

extern "C" void exception_handler(interrupt_frame* frame) {
    // some exceptions are so nasty we can't rely on printf to work
    // so we have to log exceptions this way
    if (frame->int_no == 8) {
        serial_print("EXCEPTION: ");
        serial_print(exception_names[frame->int_no]);
        serial_print("\nRIP: ");
        serial_print_hex(frame->rip);
        serial_print("\nRSP: ");
        serial_print_hex(frame->rsp);
        serial_print("\nERR: ");
        serial_print_hex(frame->error_code);
        serial_print("\n");
        for (;;) __asm__ volatile ("hlt");
    }

    printf("EXCEPTION %s!\n", exception_names[frame->int_no]);

    // can add more printfs here in the future if needed
    printf("RIP         %lu\n", frame->rip);
    printf("CS          %lu\n", frame->cs);
    printf("RFLAGS      %lu\n", frame->rflags);
    printf("SS          %lu\n", frame->ss);
    printf("RSP         %lu\n", frame->rsp);

    // since this is a fatal exception, we don't need to return from it
    for (;;) __asm__ volatile ("hlt");
}

static struct idt_entry idt[256];
static struct idtr idtr_reg;

// helper function to cut up address
static void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags, uint8_t ist) {
    uint64_t descriptor = reinterpret_cast<uint64_t>(isr);

    idt[vector].isr_low    = descriptor & 0xFFFF;
    idt[vector].kernel_cs  = 0x08;
    idt[vector].ist        = ist;
    idt[vector].attributes = flags;
    idt[vector].isr_mid    = (descriptor >> 16) & 0xFFFF;
    idt[vector].isr_high   = (descriptor >> 32) & 0xFFFFFFFF;
    idt[vector].reserved   = 0;
}

void setup_idt() {
    idtr_reg.limit = static_cast<uint16_t>((sizeof(struct idt_entry) * 256) - 1);
    idtr_reg.base  = reinterpret_cast<uint64_t>(&idt[0]);

    // populate the table
    for (int i = 0; i < 32; i++) {
        uint8_t ist = 0;

        // double fault gets it's own ist (in case the kernel runs into a stack overflow)
        if (i == 8)
            ist = 1;
        // 0x8E means: Present(1), Ring 0(00), 64-bit Interrupt Gate(01110)
        idt_set_descriptor(i, isr_stub_table[i], 0x8E, ist);
    }
    
    // load the idt
    load_idt(reinterpret_cast<uint64_t>(&idtr_reg));
}
