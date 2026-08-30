#pragma once
#include <cstdint>
#include <cstddef>
#include "ahci.hpp"
#include "../locking/lock.h"

class Drive {
protected:
    uint64_t sectors_nr;
    uint32_t sector_size;
    int drive_id;
    uint64_t hhdm_offset;
    struct spinlock lock;

public:
    Drive(uint64_t sectors_nr, uint32_t sector_size, int drive_id, uint64_t hhdm_offset) 
    : sectors_nr(sectors_nr), sector_size(sector_size), drive_id(drive_id),
        hhdm_offset(hhdm_offset) { lock.locked = false; }
    virtual ~Drive() = default;

    virtual bool read_sectors(uint64_t lba, uint32_t count, void* buffer) = 0;
    virtual bool write_sectors(uint64_t lba, uint32_t count, void* buffer) = 0;

    uint64_t get_capacity_bytes() { return sectors_nr * sector_size; }
    uint32_t get_sector_size() { return sector_size; }
};

class SATADrive : public Drive {
    private:
        HBA_PORT* port;
        int sata_port_number;
    
        // helper function for finding an empty command slot
        int find_cmd_slot();
        bool rw_sectors(uint64_t lba, uint32_t count, uint64_t phys_buffer, bool write);

    public:
        SATADrive(HBA_PORT* hba_port, int index, uint64_t hhdm_offset) 
            : Drive(0, 512, index, hhdm_offset) {
            port = hba_port;
            sata_port_number = index;
        }
    
        bool read_sectors(uint64_t lba, uint32_t count, void* buffer) override;
        bool write_sectors(uint64_t lba, uint32_t count, void* buffer) override;
};
