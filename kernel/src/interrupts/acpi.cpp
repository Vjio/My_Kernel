#include "acpi.hpp"
#include "string.hpp"
#include "stdio.hpp"
#include "io.hpp"
#include "../memory/vmm.hpp"

#define PIT_CMD_PORT  0x43
#define PIT_CH0_PORT  0x40
// register offsets
#define APIC_REGISTER_LVT_TIMER         0x320
#define APIC_REGISTER_TIMER_INITCNT     0x380
#define APIC_REGISTER_TIMER_CURRCNT     0x390
#define APIC_REGISTER_TIMER_DIV         0x3E0
// bit flags
#define APIC_LVT_INT_MASKED             0x10000
#define APIC_LVT_TIMER_MODE_PERIODIC    0x20000

volatile uint32_t *g_lapic_virt = 0;
// ECAM - enhanced configuration access mechaanism 
uint64_t g_ecam_virt = 0;
uint32_t g_apic_ticks_in_10ms = 0;

static void hardware_enable_lapic() {
    uint64_t apic_base_msr = cpu_get_msr(IA32_APIC_BASE_MSR);
    
    // set Bit 11 (Global APIC Enable)
    apic_base_msr |= IA32_APIC_BASE_MSR_ENABLE;
    
    cpu_set_msr(IA32_APIC_BASE_MSR, apic_base_msr);
}

bool acpi_validate_checksum(struct ACPISDTHeader* table_header) {
    if (table_header == nullptr) 
        return false;

    uint8_t sum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(table_header);

    for (uint32_t i = 0; i < table_header->length; i++) {
        sum += bytes[i];
    }

    return sum == 0;
}

void apic_send_eoi() {
    if (g_lapic_virt) {
        lapic_write(g_lapic_virt, 0xB0, 0); // 0xB0 is the EOI register
    }
}

void lapic_write(uint32_t volatile *lapic_base, uint32_t reg_offset, uint32_t value) {
    lapic_base[reg_offset / 4] = value;
}

static uint32_t lapic_read(uint32_t volatile *lapic_base, uint32_t reg_offset) {
    return lapic_base[reg_offset / 4];
}

uint32_t get_current_cpu_id() {
    // LAPIC ID is in the top 8 bits of register 0x20
    return lapic_read(g_lapic_virt, 0x20) >> 24;
}

static void cpu_write_IO_Apic(volatile uint32_t* io_apic_base, uint32_t reg, uint32_t value) {
   io_apic_base[0] = (reg & 0xff);
   io_apic_base[4] = value;
}

static uint32_t cpu_read_IO_apic(volatile uint32_t* io_apic_base, uint32_t reg) {
   io_apic_base[0] = (reg & 0xff);
   return io_apic_base[4];
}

void pit_prepare_sleep(uint32_t microseconds) {
    // 1.193182 MHz PIT clock
    // divisor = (frequency * microseconds) / 1000000
    uint32_t divisor = (119318 * microseconds) / 100000;

    // access Lobyte/Hibyte, mode 0 (Interrupt on terminal count)
    outb(PIT_CMD_PORT, 0x30);

    // send low byte, then high byte
    outb(PIT_CH0_PORT, divisor & 0xFF);
    outb(PIT_CH0_PORT, (divisor >> 8) & 0xFF);
}

void pit_perform_sleep() {
    uint8_t status = 0;
    // poll the PIT until the output pin (Bit 7) goes high
    do {
        // read-back command, latch status of Channel 0
        outb(PIT_CMD_PORT, 0xE2); 
        status = inb(PIT_CH0_PORT);
    } while ((status & 0x80) == 0); 
}

// https://wiki.osdev.org/APIC_Timer
void apic_start_timer() {
    // tell APIC timer to use divider 16
    lapic_write(g_lapic_virt, APIC_REGISTER_TIMER_DIV, 0x3); 

    // prepare the PIT to sleep for 10ms (10000µs)
    pit_prepare_sleep(10000); 

    // set APIC init counter to maximum (-1 or 0xFFFFFFFF)
    lapic_write(g_lapic_virt, APIC_REGISTER_TIMER_INITCNT, 0xFFFFFFFF); 

    // perform PIT-supported sleep (block until 10ms passes)
    pit_perform_sleep(); 

    // stop the APIC timer while we do math
    lapic_write(g_lapic_virt, APIC_REGISTER_LVT_TIMER, APIC_LVT_INT_MASKED); 

    // calculate how often the APIC timer ticked in 10ms
    g_apic_ticks_in_10ms = 0xFFFFFFFF - lapic_read(g_lapic_virt, APIC_REGISTER_TIMER_CURRCNT); 
    printf("APIC Calibrated: %u ticks per 10ms\n", g_apic_ticks_in_10ms);

    // start timer as periodic on IRQ 32, divider 16, with our calculated ticks
    lapic_write(g_lapic_virt, APIC_REGISTER_LVT_TIMER, 32 | APIC_LVT_TIMER_MODE_PERIODIC);
    lapic_write(g_lapic_virt, APIC_REGISTER_TIMER_DIV, 0x3);
    lapic_write(g_lapic_virt, APIC_REGISTER_TIMER_INITCNT, g_apic_ticks_in_10ms);
}

// walks the MADT and setups the ioapic
static void handle_madt(struct MADT *madt, uint64_t hhdm_offset) {
    uint64_t io_apic_phys_addr = 0;
    uint32_t keyboard_gsi = 1;  // default to Pin 1

    uint8_t* record_ptr = reinterpret_cast<uint8_t*>(madt) + sizeof(MADT);
    uint8_t* end_ptr = reinterpret_cast<uint8_t*>(madt) + madt->header.length;

    // go through all MADT entries
    // for now, we only want I/O apic
    while (record_ptr < end_ptr) {
        struct MADT_RecordHeader *header = reinterpret_cast<struct MADT_RecordHeader *>(record_ptr);

        // check if data is valid
        if (header->length == 0)
            break;

        switch (header->type) {
            case MADT_TYPE_IO_APIC: {
                struct Record_IO_APIC* ioapic = reinterpret_cast<struct Record_IO_APIC*>(record_ptr);
                io_apic_phys_addr = ioapic->io_apic_address;
                break;
            }
            case MADT_TYPE_INTERRUPT_OVERRIDE: {
                struct Record_InterruptOverride* iso = reinterpret_cast<struct Record_InterruptOverride*>(record_ptr);
                // check if this override belongs to the keyboard (IRQ 1)
                if (iso->irq_source == 1) {
                    keyboard_gsi = iso->global_interrupt;
                }
                break;
            }
        }
        record_ptr += header->length; // go to next record
    }
    
    hardware_enable_lapic();
    
    volatile uint32_t* lapic_virt = reinterpret_cast<volatile uint32_t*>(madt->local_apic_address + hhdm_offset);
    volatile uint32_t* io_apic_virt = reinterpret_cast<volatile uint32_t*>(io_apic_phys_addr + hhdm_offset);
    g_lapic_virt = lapic_virt;

    VMM::map_page(nullptr, reinterpret_cast<uint64_t>(lapic_virt),
        static_cast<uint64_t>(madt->local_apic_address),
        PTE_PRESENT | PTE_READ_WRITE | PTE_CACHE_DISABLE);
        
    VMM::map_page(nullptr, reinterpret_cast<uint64_t>(io_apic_virt), io_apic_phys_addr, 
        PTE_PRESENT | PTE_READ_WRITE | PTE_CACHE_DISABLE);

    printf("LAPIC Phys: 0x%x, Virt: 0x%lx\n", madt->local_apic_address, (uint64_t)lapic_virt);

    // offset 0xF0 is the Spurious Interrupt Vector Register
    uint32_t sivr = lapic_read(lapic_virt, 0xF0);
    sivr |= (1 << 8);     // bit 8: APIC Software Enable
    sivr |= 0xFF;         // low 8 bits: Spurious Vector (Vector 255)
    lapic_write(lapic_virt, 0xF0, sivr);

    // program the Redirection Table Entry for the Keyboard
    uint32_t target_vector = 33; // vector 33 (IRQ 1 mapped to 32+1)
    uint32_t low_index = 0x10 + (2 * keyboard_gsi);
    uint32_t high_index = low_index + 1;

    // write target destination (Core 0 APIC ID) to high 32 bits
    cpu_write_IO_Apic(io_apic_virt, high_index, 0 << 24);

    // write vector configuration and unmask to low 32 bits
    cpu_write_IO_Apic(io_apic_virt, low_index, target_vector);

    apic_start_timer();
    __asm__ volatile ("sti");
}

static void handle_mcfg(struct MCFG *mcfg, uint64_t hhdm_offset) {
    int mcfg_entries = (mcfg->header.length - sizeof(struct MCFG)) / sizeof(struct MCFG_Allocation);
        
    for (int i = 0; i < mcfg_entries; i++) {
        uint64_t ecam_phys = mcfg->allocations[i].base_address;
        uint8_t start_bus = mcfg->allocations[i].start_bus;
        uint8_t end_bus = mcfg->allocations[i].end_bus;

        // calculate total size, 1 MB per bus
        // (end_bus + 1 - start_bus) gives total buses in this allocation
        uint64_t total_bytes = (end_bus + 1 - start_bus) * 1024 * 1024; 
        uint64_t ecam_virt = ecam_phys + hhdm_offset;

        for (uint64_t offset = 0; offset < total_bytes; offset += 4096) {
            if (!VMM::map_page(nullptr, ecam_virt + offset, ecam_phys + offset, 
                            PTE_PRESENT | PTE_READ_WRITE | PTE_CACHE_DISABLE)) {
                // this should never happen. no need to write a failure path
                printf("handle mcfg failed!\n");
            }
        }
        
        g_ecam_virt = ecam_virt;
    }
}

bool setup_acpi(struct RSDP2 *rsdp, uint64_t hhdm_offset) {
    // get XSDT from RSDP
    struct ACPISDTHeader *xsdt = reinterpret_cast<struct ACPISDTHeader *>(rsdp->xsdt_address + hhdm_offset);

    // check if table is valid
    if (!acpi_validate_checksum(xsdt))
        return false;

    // the XSDT is followed by pointers to other tables
    // calculate how many entries that table has
    int entries = (xsdt->length - sizeof(struct ACPISDTHeader)) / 8;

    // get a pointer to start of array
    uint64_t *xsdt_entries = reinterpret_cast<uint64_t *>
        (reinterpret_cast<uint64_t> (xsdt) + sizeof(struct ACPISDTHeader));

    struct MADT *madt = nullptr;
    struct MCFG *mcfg = nullptr;

    // loop through array
    for (int i = 0; i < entries; i++) {
        struct ACPISDTHeader *curr_entry = reinterpret_cast<struct ACPISDTHeader *>(xsdt_entries[i] + hhdm_offset);

        if (strncmp(curr_entry->signature, "APIC", 4) == 0 ) {
            if (!acpi_validate_checksum(curr_entry))
                return false;

            madt = reinterpret_cast<struct MADT *>(curr_entry);
        }
        else if (strncmp(curr_entry->signature, "MCFG", 4) == 0) {
            if (!acpi_validate_checksum(curr_entry)) 
                return false;
            mcfg = reinterpret_cast<struct MCFG *>(curr_entry);
        }
    }

    if (madt == nullptr || mcfg == nullptr) {
        printf("setup acpi failed!\n");
        return false;
    }

    handle_madt(madt, hhdm_offset);
    handle_mcfg(mcfg, hhdm_offset);

    return true;
}
