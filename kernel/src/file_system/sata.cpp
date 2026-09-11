#include "sata.hpp"
#include "../stdio.hpp"
#include "../memory/memory.hpp"

int SATADrive::find_cmd_slot() {
    for (int i = 0; i < 32; i++)
        if (!command_slots[i].reserved)
            return i;
    return -1;
}

int SATADrive::reserve_slot() {
    acquire(&lock);
    int slot = find_cmd_slot();
    if (slot != -1)
        command_slots[slot].reserved = true;
    release(&lock);
    return slot;
}

static bool ata_command_is_write(uint8_t command) {
    switch (command) {
        case ATA_CMD_WRITE_DMA_EX:
            return true;
        default:
            return false;
    }
}

static void build_h2d_fis(FIS_REG_H2D *fis, uint64_t lba, uint16_t count, uint8_t command) {
    // clear any previous command
    memset(fis, 0, sizeof(FIS_REG_H2D));

    fis->fis_type = FIS_TYPE_REG_H2D;
    // 1 stands for command
    fis->c        = 1;
    fis->command  = command;

    fis->lba0 = (uint8_t)(lba >> 0);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->device = 1 << 6;

    fis->countl = (uint8_t)(count >> 0);
    fis->counth = (uint8_t)(count >> 8);
}


void SATADrive::release_slot(int slot) {
    acquire(&lock);
    command_slots[slot].reserved       = false;
    command_slots[slot].issued         = false;
    command_slots[slot].done           = false;
    command_slots[slot].failure        = false;
    command_slots[slot].waiting_thread = nullptr;
    release(&lock);
}

HBA_CMD_TBL *SATADrive::get_cmd_table(int slot) {
    HBA_CMD_HEADER &hdr = cmd_list[slot];
    uint64_t phys = ((uint64_t)hdr.ctbau << 32) | hdr.ctba;
    return reinterpret_cast<HBA_CMD_TBL *>(phys + hhdm_offset);
}

static void build_prdt(HBA_CMD_HEADER *cmd_header, HBA_CMD_TBL *tbl,
    uint64_t phys_addr, uint32_t byte_count) {
    memset(&tbl->prdt_entry[0], 0, sizeof(HBA_PRDT_ENTRY));

    tbl->prdt_entry[0].dba  = (uint32_t)(phys_addr & 0xFFFFFFFF);
    tbl->prdt_entry[0].dbau = (uint32_t)(phys_addr >> 32);
    tbl->prdt_entry[0].dbc  = byte_count - 1;
    tbl->prdt_entry[0].i    = 1;

    cmd_header->prdtl = 1;
}

bool SATADrive::send_command(int slot, uint8_t command, uint64_t lba,
                                uint16_t count, uint32_t byte_count) {
    // this spinning while will get removed once driver is updated to handle NCQ commands
    while (port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ));

    HBA_CMD_HEADER *cmd_header = &cmd_list[slot];
    HBA_CMD_TBL    *tbl        = get_cmd_table(slot);
    // clear stale cfis/prdt from a previous command
    memset(tbl, 0, sizeof(HBA_CMD_TBL));

    cmd_header->cfl   = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmd_header->w     = ata_command_is_write(command);
    cmd_header->c     = 1; // clear busy on R_OK
    cmd_header->prdbc = 0;

    build_h2d_fis(reinterpret_cast<FIS_REG_H2D*>(tbl->cfis), lba, count, command);

    if (byte_count > 0) {
        build_prdt(cmd_header, tbl, command_slots[slot].bounce_phys, byte_count);
    } else {
        cmd_header->prdtl = 0;
    }

    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags));

    struct thread *thread = Scheduler::get_current_scheduler()->get_running_thread();
    command_slots[slot].issued          = true;
    command_slots[slot].done            = false;
    command_slots[slot].failure         = false;
    command_slots[slot].waiting_thread  = thread;
    thread->status                      = WAITING;

    // issue command
    port->ci |= (1u << slot);

    if (flags & (1u << 9))
        __asm__ volatile("sti");

    // fast AHCI command completion could mean the command finishes even before this function
    // gets called. thus, you have to make sure the thread is set to waiting
    // BEFORE issuing the command
    thread->wait_until_taken_out_of_waiting();

    return !command_slots[slot].failure;
}

bool SATADrive::rw_sectors(uint64_t lba, uint32_t count, void *buffer, uint8_t user_cmd) {
    uint8_t *cursor    = reinterpret_cast<uint8_t*>(buffer);
    uint32_t remaining = count;
    uint64_t cur_lba   = lba;

    uint32_t max_chunk_sectors = BOUNCE_BUFFER_SIZE / sector_size;
    while (remaining > 0) {
        uint32_t chunk      = remaining < max_chunk_sectors ? remaining : max_chunk_sectors;
        uint32_t byte_count = chunk * sector_size;

        int slot = reserve_slot();
        if (slot == -1)
            return false;

        if (user_cmd == ATA_CMD_WRITE_DMA_EX)
            memcpy(reinterpret_cast<void *>(command_slots[slot].bounce_virt), cursor, byte_count);

        bool ok = send_command(slot, user_cmd, cur_lba, (uint16_t)chunk, byte_count);

        if (ok && user_cmd == ATA_CMD_READ_DMA_EX)
            memcpy(cursor, reinterpret_cast<void *>(command_slots[slot].bounce_virt), byte_count);
        if (!ok)
            return false;

        release_slot(slot);

        cursor    += byte_count;
        cur_lba   += chunk;
        remaining -= chunk;
    }

    return true;
}

bool SATADrive::read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    return rw_sectors(lba, count, buffer, ATA_CMD_READ_DMA_EX);
}

bool SATADrive::write_sectors(uint64_t lba, uint32_t count, void* buffer) {
    return rw_sectors(lba, count, buffer, ATA_CMD_WRITE_DMA_EX);
}

void SATADrive::handle_non_ncq_error() {
    HBA_PORT *port = get_port();
    ahci_pause_cmd(port);

    // on failure, the driver will not try to resend the command
    // instead, it will just propagate the failure to the sleeping thread
    for (int slot = 0; slot < 32; slot++) {
        if (command_slots[slot].issued && !command_slots[slot].done) {
            command_slots[slot].done = true;
            command_slots[slot].issued = false;
            command_slots[slot].failure = true;
            // TODO: could catch the status code and send it to the waiting thread. doesn't seem needed currently

            if (command_slots[slot].waiting_thread) {
                command_slots[slot].waiting_thread->take_thread_out_of_waiting();
                command_slots[slot].waiting_thread = nullptr;
            }
        }
    }

    // clear int registers
    port->serr = 0xFFFFFFFF;
    port->is   = 0xFFFFFFFF;

    ahci_resume_cmd(port);
}

void SATADrive::handle_port_completion() {
    HBA_PORT *port = get_port();

    // all bits set in the ci are command that are still in progress
    uint32_t ci = port->ci;
    for (int i = 0; i < 32; i++) {
        SATA_command_slot &slot = command_slots[i];

        if (!slot.issued || (ci & (1u << i)))
            continue;

        slot.issued  = false;
        slot.done    = true;
        slot.failure = false;

        // notify waiting thread
        slot.waiting_thread->take_thread_out_of_waiting();
        slot.waiting_thread = nullptr;
    }

    port->is = port->is;
}

bool SATADrive::init_bounce_buffers() {
    uint64_t frames_needed = BOUNCE_BUFFER_SIZE / FRAME_SIZE;

    for (int i = 0; i < 32; i++) {
        uint64_t phys = PMM::alloc_frames(frames_needed);
        if (!phys) {
            printf("SATADrive: bounce buffer alloc failed for slot %d\n", i);
            // TODO: if this actually happens, write a failure path that frees the allocated memory
            return false;
        }

        command_slots[i].bounce_phys = phys;
        command_slots[i].bounce_virt = phys + hhdm_offset;
    }

    return true;
}

void SATADrive::identify() {
    printf("sending identify command!\n");
    int slot = reserve_slot();
    // IDENTIFY DEVICE ignores LBA/count it just returns a fixed 512-byte struct
    bool ok = send_command(slot, ATA_CMD_IDENTIFY_DEVICE, 0, 0, 512);
    if (!ok) {
        // TODO: write a graceful failure path if this happens
        printf("SATADrive: IDENTIFY command failed\n");
    }

    uint16_t *id = reinterpret_cast<uint16_t*>(command_slots[slot].bounce_virt);

    // words 100-103: 48-bit LBA total addressable sectors
    uint64_t lba48_sectors =
          (uint64_t)id[100]
        | ((uint64_t)id[101] << 16)
        | ((uint64_t)id[102] << 32)
        | ((uint64_t)id[103] << 48);

    // word 106 bit 12: set if logical sector size > 256 words (i.e. != 512 bytes)
    // words 117-118: logical sector size in words, valid only if that bit is set
    uint32_t sector_sz = 512;
    if (id[106] & (1 << 12)) {
        uint32_t words = (uint32_t)id[117] | ((uint32_t)id[118] << 16);
        sector_sz = words * 2;
    }

    sectors_nr  = lba48_sectors;
    sector_size = sector_sz;
    release_slot(slot);
}


