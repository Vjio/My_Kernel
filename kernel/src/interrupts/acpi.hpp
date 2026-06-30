#pragma once
#include <cstdint>
#include <cstddef>

// ACPI -> Advanced Configuration and Power Interface
#define IA32_APIC_BASE_MSR        0x1B
#define IA32_APIC_BASE_MSR_ENABLE (1 << 11)

struct RSDP2 {
    char signature[8];          // magic number for locating RSDP
    uint8_t checksum;           // used to verify the first 20 bytes of RSDP
    char OEMID[6];              // OEM-supplied string
    uint8_t revision;           // RSDP revision (version)
    uint32_t rsdt_address;      // deprecated since version 2.0. pointed to RSDP revision 1

    uint32_t length;            // size of RSDP
    uint64_t xsdt_address;      // points to the RSDP version 2 (also called XSDT)
    uint8_t extended_checksum;  // checksum for the entire table
    uint8_t reserved[3];
} __attribute__ ((packed));

// ACPI system descriptor header
struct ACPISDTHeader {
    char signature[4];          // each table has an assigned magic number
    uint32_t length;            // total size of table (including header)
    uint8_t revision;           // version
    uint8_t checksum;           // checksum
    char OEMID[6];              // OEM-supplied string
    char OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t creator_ID;
    uint32_t creator_revision;
} __attribute__((packed));

#define MADT_TYPE_LOCAL_APIC           0
#define MADT_TYPE_IO_APIC              1
#define MADT_TYPE_INTERRUPT_OVERRIDE   2

// Multiple APIC Description Table
struct MADT {
    ACPISDTHeader header;
    uint32_t local_apic_address; // default physical address of the Local APIC
    uint32_t flags;
    // the MADT is followed by a list of variable length APIC entries
} __attribute__((packed));

struct MADT_RecordHeader {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct Record_IO_APIC {
    MADT_RecordHeader header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;   // physical address of the I/O APIC
    uint32_t global_system_interrupt_base;
} __attribute__((packed));

struct Record_InterruptOverride {
    MADT_RecordHeader header;
    uint8_t bus_source;         // always 0 for ISA
    uint8_t irq_source;         // legacy IRQ number (ex: 1 for keyboard)
    uint32_t global_interrupt;  // physical input pin on the I/O APIC it actually connects to
    uint16_t flags;             // polarity and trigger mode (Edge vs Level)
} __attribute__((packed));

// helper function for validiaing ACPI checksums
bool acpi_validate_checksum(ACPISDTHeader* table_header);

bool setup_acpi(struct RSDP2 *rsdp, uint64_t hhdm_offset);
