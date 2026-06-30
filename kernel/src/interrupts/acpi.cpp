#include "acpi.hpp"
#include "string.hpp"
#include "stdio.hpp"
#include "io.hpp"

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

static void lapic_write(uint32_t volatile *lapic_base, uint32_t reg_offset, uint32_t value) {
    lapic_base[reg_offset / 4] = value;
}

static uint32_t lapic_read(uint32_t volatile *lapic_base, uint32_t reg_offset) {
    return lapic_base[reg_offset / 4];
}

static void cpu_write_IO_Apic(volatile uint32_t* io_apic_base, uint32_t reg, uint32_t value) {
   io_apic_base[0] = (reg & 0xff);
   io_apic_base[4] = value;
}

static uint32_t cpu_read_IO_apic(volatile uint32_t* io_apic_base, uint32_t reg) {
   io_apic_base[0] = (reg & 0xff);
   return io_apic_base[4];
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

    // loop through array
    for (int i = 0; i < entries; i++) {
        struct ACPISDTHeader *curr_entry = reinterpret_cast<struct ACPISDTHeader *>(xsdt_entries[i] + hhdm_offset);

        if (strncmp(curr_entry->signature, "APIC", 4) == 0 ) {
            if (!acpi_validate_checksum(curr_entry))
                return false;

            madt = reinterpret_cast<struct MADT *>(curr_entry);
        }
    }

    if (madt == nullptr)
        return false;

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

    volatile uint32_t* lapic_base = reinterpret_cast<volatile uint32_t*>(madt->local_apic_address + hhdm_offset);
    volatile uint32_t* io_apic_base = reinterpret_cast<volatile uint32_t*>(io_apic_phys_addr + hhdm_offset);

    printf("LAPIC Phys: 0x%x, Virt: 0x%lx\n", madt->local_apic_address, (uint64_t)lapic_base);

    // offset 0xF0 is the Spurious Interrupt Vector Register
    uint32_t sivr = lapic_read(lapic_base, 0xF0);
    sivr |= (1 << 8);     // Bit 8: APIC Software Enable
    sivr |= 0xFF;         // Low 8 bits: Spurious Vector (Vector 255)
    lapic_write(lapic_base, 0xF0, sivr);

    // program the Redirection Table Entry for the Keyboard
    uint32_t target_vector = 33; // Vector 33 (IRQ 1 mapped to 32+1)
    uint32_t low_index = 0x10 + (2 * keyboard_gsi);
    uint32_t high_index = low_index + 1;

    // Write target destination (Core 0 APIC ID) to high 32 bits
    cpu_write_IO_Apic(io_apic_base, high_index, 0 << 24);

    // Write vector configuration and unmask to low 32 bits
    cpu_write_IO_Apic(io_apic_base, low_index, target_vector);

    __asm__ volatile ("sti");

    return true;
}
