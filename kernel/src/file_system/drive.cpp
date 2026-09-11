#include "drive.hpp"
#include "../memory/memory.hpp"
#include "../memory/vmm.hpp"
#include "../stdio.hpp"
#include "sata.hpp"

bool Drive::disk_write(uint64_t byte_offset, uint32_t size, void *buffer) {
    // we will split the buffer into 3 parts, head, middle and tail
    // head and tail will have their own buffers. writing from the middle part will be
    // done straight from the users buffer
    uint64_t first_lba    = byte_offset / sector_size;
    uint32_t head_start   = byte_offset % sector_size;
    uint64_t last_byte    = byte_offset + size - 1;
    uint64_t last_lba     = last_byte / sector_size;
    uint32_t tail_used    = (uint32_t)(last_byte % sector_size) + 1;

    uint8_t *buffer_casted = reinterpret_cast<uint8_t *>(buffer);
    uint32_t buffer_cursor = 0;
    bool ok;

    // perfect case: the whole buffer fits in a single sector
    if (first_lba == last_lba) {
        if (head_start == 0 && size == sector_size)
            return write_sectors(first_lba, 1, buffer_casted);

        uint8_t *temp_buf = reinterpret_cast<uint8_t *>(malloc(sector_size));
        if (!read_sectors(first_lba, 1, temp_buf)) {
            free(temp_buf);
            return false;
        }
        memcpy(temp_buf + head_start, buffer_casted, size);
        ok = write_sectors(first_lba, 1, temp_buf);
        free(temp_buf);
        return ok;
    }

    // head
    if (head_start != 0) {
        uint8_t *head_buf = reinterpret_cast<uint8_t *>(malloc(sector_size));
        if (!read_sectors(first_lba, 1, head_buf)) {
            free(head_buf);
            return false;
        }

        uint32_t head_size = sector_size - head_start;
        memcpy(head_buf + head_start, buffer_casted, head_size);

        ok = write_sectors(first_lba, 1, head_buf);
        free(head_buf);
        if (!ok) 
            return false;
        buffer_cursor += head_size;
    }

    // middle. this part is alligned to sector_size, extra buffers will not be used
    uint64_t middle_start = first_lba + (head_start != 0 ? 1 : 0);
    uint64_t middle_end   = last_lba  - (tail_used != sector_size ? 1 : 0);
    if (middle_end >= middle_start) {
        uint32_t middle_sectors = (uint32_t)(middle_end - middle_start + 1);
        if (!write_sectors(middle_start, middle_sectors, buffer_casted + buffer_cursor))
            return false;
        buffer_cursor += middle_sectors * sector_size;
    }

    // tail
    if (tail_used != sector_size) {
        uint8_t *tail_buf = reinterpret_cast<uint8_t *>(malloc(sector_size));
        if (!read_sectors(last_lba, 1, tail_buf)) {
            free(tail_buf);
            return false;
        }

        memcpy(tail_buf, buffer_casted + buffer_cursor, tail_used);

        ok = write_sectors(last_lba, 1, tail_buf);
        free(tail_buf);
        if (!ok) 
            return false;
    }

    return true;
}

bool Drive::disk_read(uint64_t byte_offset, uint32_t size, void *buffer) {
    uint64_t first_lba    = byte_offset / sector_size;
    uint32_t head_start   = byte_offset % sector_size;
    uint64_t last_byte    = byte_offset + size - 1;
    uint64_t last_lba     = last_byte / sector_size;
    uint32_t tail_used    = (uint32_t)(last_byte % sector_size) + 1;

    uint8_t *buffer_casted = reinterpret_cast<uint8_t *>(buffer);
    uint32_t buffer_cursor = 0;

    if (size == 0)
        return true;

    // perfect case: the whole request fits in a single sector
    if (first_lba == last_lba) {
        if (head_start == 0 && size == sector_size)
            return read_sectors(first_lba, 1, buffer_casted);

        uint8_t *temp_buf = reinterpret_cast<uint8_t *>(malloc(sector_size));
        if (!read_sectors(first_lba, 1, temp_buf)) {
            free(temp_buf);
            return false;
        }
        memcpy(buffer_casted, temp_buf + head_start, size);
        free(temp_buf);
        return true;
    }

    // head
    if (head_start != 0) {
        uint8_t *head_buf = reinterpret_cast<uint8_t *>(malloc(sector_size));
        if (!read_sectors(first_lba, 1, head_buf)) {
            free(head_buf);
            return false;
        }

        uint32_t head_size = sector_size - head_start;
        memcpy(buffer_casted, head_buf + head_start, head_size);
        free(head_buf);
        buffer_cursor += head_size;
    }

    // middle
    uint64_t middle_start = first_lba + (head_start != 0 ? 1 : 0);
    uint64_t middle_end   = last_lba  - (tail_used != sector_size ? 1 : 0);
    if (middle_end >= middle_start) {
        uint32_t middle_sectors = (uint32_t)(middle_end - middle_start + 1);
        if (!read_sectors(middle_start, middle_sectors, buffer_casted + buffer_cursor))
            return false;
        buffer_cursor += middle_sectors * sector_size;
    }

    // tail
    if (tail_used != sector_size) {
        uint8_t *tail_buf = reinterpret_cast<uint8_t *>(malloc(sector_size));
        if (!read_sectors(last_lba, 1, tail_buf)) {
            free(tail_buf);
            return false;
        }

        memcpy(buffer_casted + buffer_cursor, tail_buf, tail_used);
        free(tail_buf);
    }

    return true;
}

void hard_drive_handle_interrupt(interrupt_frame *frame) {
    HBA_MEM *abar = Drive::abar;

    // global interrupt status, bit per port
    uint32_t pending = abar->is;
    for (int i = 0; i < AHCI_PROBE_COUNT; i++) {
        if (!Drive::drives[i]) 
            continue;
        if (!(pending & (1u << i))) 
            continue;

        SATADrive *drive = static_cast<SATADrive*>(Drive::drives[i]);
        HBA_PORT *port = drive->get_port();
        uint32_t port_is = port->is;

        // check for erroes
        if (port_is & HBA_PxIS_ERR_MASK) {
            drive->handle_non_ncq_error();

        } else {
            drive->handle_port_completion();
        }

        port->is = 0xFFFFFFFF;
    }

    // clear interrupt status
    abar->is = 0xFFFFFFFF;
}

void Drive::zero_out_drive() {
    uint8_t buf[sector_size];
    memset(buf, 0, sector_size);

    for(uint64_t i = 0; i < sectors_nr; i++)
        write_sectors(i, 1, buf);
}
