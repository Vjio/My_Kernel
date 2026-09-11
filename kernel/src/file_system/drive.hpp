#pragma once
#include <cstdint>
#include <cstddef>
#include "ahci.hpp"
#include "../locking/lock.h"
#include "../interrupts/idt.hpp"
#include "../scheduling/scheduler.hpp"
#include "../memory/pmm.hpp"

#define MAX_CMD_RETRIES         3
#define BOUNCE_BUFFER_SIZE      FRAME_SIZE * 4
#define BOUNCE_BUFFER_SECTORS   BOUNCE_BUFFER_SIZE / 512

class Drive {
protected:
    uint64_t sectors_nr;
    uint32_t sector_size;
    int drive_id;
    uint64_t hhdm_offset;
    struct spinlock lock;

    // lba   - sector to start from
    // count - the number of sectors to read
    virtual bool read_sectors(uint64_t lba, uint32_t count, void* buffer) = 0;
    // lba   - sector to start from
    // count - the number of sectors to write
    virtual bool write_sectors(uint64_t lba, uint32_t count, void* buffer) = 0;
public:
    inline static uint8_t interrupt_line = 0;
    inline static Drive *drives[AHCI_PROBE_COUNT] = {};
    inline static HBA_MEM *abar = nullptr; 

    Drive(uint64_t sectors_nr, uint32_t sector_size, int drive_id, uint64_t hhdm_offset) 
    : sectors_nr(sectors_nr), sector_size(sector_size), drive_id(drive_id),
        hhdm_offset(hhdm_offset) { lock.locked = false; }
    virtual ~Drive() = default;

    bool disk_write(uint64_t byte_offset, uint32_t size, void *buffer);
    bool disk_read(uint64_t byte_offset, uint32_t size, void *buffer);
    // beware, very inneficient helper!
    void zero_out_drive();

    uint64_t get_capacity_bytes() { return sectors_nr * sector_size; }
    uint32_t get_sector_size() { return sector_size; }
};

void hard_drive_handle_interrupt(interrupt_frame *frame);
