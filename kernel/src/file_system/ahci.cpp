#include "ahci.hpp"
#include "drive.hpp"
#include "stdio.hpp"
#include "../memory/memory.hpp"
#include "../memory/pmm.hpp"

#define HBA_PxCMD_ST            0x0001
#define HBA_PxCMD_FRE           0x0010
#define HBA_PxCMD_FR            0x4000
#define HBA_PxCMD_CR            0x8000
#define DEFAULT_PRDT_ENTRIES    8

void ahci_pause_cmd(HBA_PORT *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    while (port->cmd & HBA_PxCMD_CR);
}

void ahci_resume_cmd(HBA_PORT *port) {
    while (port->cmd & HBA_PxCMD_CR);
    port->cmd |= HBA_PxCMD_ST;
}

static void ahci_stop_cmd(HBA_PORT *port) {
    // clear start and fre (FIS Receive Enable)
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    // wait until flags get cleared
    while(true) {
        if ((port->cmd & HBA_PxCMD_FR) == 0 && (port->cmd & HBA_PxCMD_CR) == 0) {
            break;
        }
    }
}

static void ahci_start_cmd(HBA_PORT *port) {
    // wait until CR is clear before setting start and FRE
    while (port->cmd & HBA_PxCMD_CR);

    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

// prepares port and maps needed structs for it in memory
// returns the command table where the structs where mapped
static HBA_CMD_HEADER* ahci_port_rebase(HBA_PORT *port, uint64_t hhdm_offset) {
    ahci_stop_cmd(port);

    // allocate space for command list
    // clb only needs 1kb so here we waste 3kb. not really a big issue
    uint64_t clb_phys = PMM::alloc_frame(); 
    port->clb  = (uint32_t)(clb_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(clb_phys >> 32);

    uint64_t clb_virt = clb_phys + hhdm_offset;
    memset(reinterpret_cast<void*>(clb_virt), 0, 1024);

    // fis receive buffer
    uint64_t fb_phys = PMM::alloc_frame(); 
    port->fb  = (uint32_t)(fb_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fb_phys >> 32);

    uint64_t fb_virt = fb_phys + hhdm_offset;
    memset(reinterpret_cast<void*>(fb_virt), 0, 256);

    // command tables, one for each of the 32 slots in the command list
    HBA_CMD_HEADER *cmdheader = reinterpret_cast<HBA_CMD_HEADER*>(clb_virt);

    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = DEFAULT_PRDT_ENTRIES;

        uint64_t cmd_table_phys = PMM::alloc_frame();
        cmdheader[i].ctba  = (uint32_t)(cmd_table_phys & 0xFFFFFFFF);
        cmdheader[i].ctbau = (uint32_t)(cmd_table_phys >> 32);

        uint64_t cmd_table_virt = cmd_table_phys + hhdm_offset;
        memset(reinterpret_cast<void*>(cmd_table_virt), 0, 256);
    }

    // clear any stale interrupts
    port->serr = 0xFFFFFFFF;
    port->is   = 0xFFFFFFFF;
    port->ie   = HBA_PxIE_ENABLE_MASK;

    ahci_start_cmd(port);
    return cmdheader;
}

static int check_port_type(HBA_PORT *port) {
    uint32_t ssts = port->ssts;

    // DET - Device Detection
    // IPM - Interface Power Management
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT) 
        return 0;
    if (ipm != HBA_PORT_IPM_ACTIVE) 
        return 0;

    return port->sig;
}

void ahci_init(uint64_t abar_address, uint64_t hhdm_offset) {
    HBA_MEM *abar = reinterpret_cast<HBA_MEM *>(abar_address);
    abar->ghc |= HBA_GHC_AE;
    Drive::abar = abar;

    uint32_t pi = abar->pi;
    // probe all ports
    for (int i = 0; i < AHCI_PROBE_COUNT; i++) {
        if (pi & (1 << i)) {
            // bit is set,
            // check if a device is plugged in and active
            int dev_type = check_port_type(&abar->ports[i]);

            if (dev_type == AHCI_DEV_SATA) {
                HBA_CMD_HEADER *cmd_list = ahci_port_rebase(&abar->ports[i], hhdm_offset);

                SATADrive *drive = new SATADrive(&abar->ports[i], i, hhdm_offset, cmd_list);
                if (!drive->init_bounce_buffers()) {
                    // this should not happen, dont bother writing a failure path
                    printf("SATADrive: failed to init!");
                    delete drive;
                    continue;
                }

                Drive::drives[i] = drive;
            }
        }
    }

    // enable interrupt line
    abar->is = 0xFFFFFFFF;
    abar->ghc |= HBA_GHC_IE;

    // set up drive geometry
    for (int i = 0; i < AHCI_PROBE_COUNT; i++) {
        if (Drive::drives[i]) {
            static_cast<SATADrive*>(Drive::drives[i])->identify();
        }
    }
}

void reset_port(HBA_PORT *port) {
    ahci_stop_cmd(port);
    // clear serr and is. write 1 to clear
    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;
    ahci_start_cmd(port);
}
