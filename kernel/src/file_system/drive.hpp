#pragma once
#include <cstdint>
#include <cstddef>
#include "ahci.hpp"
#include "../locking/lock.h"
#include "../interrupts/idt.hpp"
#include "../scheduling/process.hpp"
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

public:
    inline static uint8_t interrupt_line = 0;
    inline static Drive *drives[AHCI_PROBE_COUNT] = {};
    inline static HBA_MEM *abar = nullptr; 

    Drive(uint64_t sectors_nr, uint32_t sector_size, int drive_id, uint64_t hhdm_offset) 
    : sectors_nr(sectors_nr), sector_size(sector_size), drive_id(drive_id),
        hhdm_offset(hhdm_offset) { lock.locked = false; }
    virtual ~Drive() = default;

    // lba   - sector to start from
    // count - the number of sectors to read
    virtual bool read_sectors(uint64_t lba, uint32_t count, void* buffer) = 0;
    // lba   - sector to start from
    // count - the number of sectors to write
    virtual bool write_sectors(uint64_t lba, uint32_t count, void* buffer) = 0;
    // beware, very inneficient helper!
    void zero_out_drive();

    uint64_t get_capacity_bytes() { return sectors_nr * sector_size; }
    uint32_t get_sector_size() { return sector_size; }
};

struct SATA_command_slot {
    struct thread *waiting_thread = nullptr;
    bool issued                   = false;
    bool done                     = false;
    bool failure                  = false;
    bool reserved                 = false;
    uint64_t bounce_phys          = 0;
    uint64_t bounce_virt          = 0;
};

class SATADrive : public Drive {
    private:
        HBA_PORT* port;
        int sata_port_number;
        struct SATA_command_slot command_slots[32];
        HBA_CMD_HEADER *cmd_list;

        // find the first unused command slot. doesn't modify anything in the array
        int find_cmd_slot();
        // find and reserves the first unused command slot. thread-safe
        int reserve_slot();
        void release_slot(int slot);
        HBA_CMD_TBL *get_cmd_table(int slot);
        bool send_command(int slot, uint8_t command, uint64_t lba,
                            uint16_t count, uint32_t byte_count);
        bool rw_sectors(uint64_t lba, uint32_t count, void *buffer, uint8_t user_cmd);

    public:
        SATADrive(HBA_PORT* hba_port, int index, uint64_t hhdm_offset, HBA_CMD_HEADER *cmd_list) 
        : Drive(0, 512, index, hhdm_offset) {
            port = hba_port;
            sata_port_number = index;
            this->cmd_list = cmd_list;
        }

        HBA_PORT *get_port() {
            return port;
        }

        // sets up drive geometry
        void identify();

        bool read_sectors(uint64_t lba, uint32_t count, void* buffer) override;
        bool write_sectors(uint64_t lba, uint32_t count, void* buffer) override;
        void handle_non_ncq_error();
        void handle_port_completion();
        bool init_bounce_buffers();
};

void hard_drive_handle_interrupt(interrupt_frame *frame);
