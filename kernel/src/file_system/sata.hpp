#pragma once
#include <cstdint>
#include <cstddef>
#include "ahci.hpp"
#include "drive.hpp"
#include "../scheduling/process.hpp"

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
        bool read_sectors(uint64_t lba, uint32_t count, void* buffer) override;
        bool write_sectors(uint64_t lba, uint32_t count, void* buffer) override;
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

        void handle_non_ncq_error();
        void handle_port_completion();
        bool init_bounce_buffers();
};