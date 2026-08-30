#include "drive.hpp"
#include "../memory/memory.hpp"
#include "../memory/vmm.hpp"

int SATADrive::find_cmd_slot() {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++)
        if ((slots & (1 << i)) == 0) 
            return i;
    return -1;
}

bool SATADrive::rw_sectors(uint64_t lba, uint32_t count, uint64_t phys_buffer, bool write) {
    // STUB
    return true;
}

bool SATADrive::read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    uint64_t phys_addr = VMM::get_physical_address(buffer); 
    if (!phys_addr)
        return false;

    return rw_sectors(lba, count, phys_addr, false);
}

bool SATADrive::write_sectors(uint64_t lba, uint32_t count, void* buffer) {
    uint64_t phys_addr = VMM::get_physical_address(buffer);
    if (!phys_addr)
        return false;

    return rw_sectors(lba, count, phys_addr, true);
}
