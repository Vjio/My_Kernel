#include "pci.hpp"
#include "acpi.hpp"
#include "stdio.hpp"
#include "../memory/vmm.hpp"

extern uint64_t g_ecam_virt;

static struct PCI_Header* get_pci_device(uint8_t bus, uint8_t device, uint8_t function) {
    uint64_t offset = ((uint64_t)bus << 20) | ((uint64_t)device << 15) | ((uint64_t)function << 12);
    return reinterpret_cast<struct PCI_Header*>(g_ecam_virt + offset);
}

uint64_t find_ahci_controller(uint64_t hhdm_offset) {
    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            for (int function = 0; function < 8; function++) {
                struct PCI_Header* pci_dev = get_pci_device(bus, device, function);
                
                // 0xFFFF means no device is connected to this BDF
                if (pci_dev->vendor_id == 0xFFFF) {
                    continue; 
                }

                // is it an AHCI controller?
                if (pci_dev->class_code == 0x01 && 
                    pci_dev->subclass == 0x06 && 
                    pci_dev->prog_if == 0x01) {
                    
                    printf("AHCI Controller found at %d:%d:%d\n", bus, device, function);

                    // BAR5 contains the physical memory address of the AHCI registers
                    uint32_t abar_phys = pci_dev->bar5;
                    
                    // clear the low 4 bits (they are status flags, not part of the address)
                    abar_phys &= 0xFFFFFFF0;

                    pci_dev->command |= (1 << 2) | (1 << 1); 

                    uint64_t abar_virt = abar_phys + hhdm_offset;
                    VMM::map_page(nullptr, abar_virt, abar_phys, 
                                  PTE_PRESENT | PTE_READ_WRITE | PTE_CACHE_DISABLE);
                    
                    return abar_virt;
                }
            }
        }
    }
    // this should not happen. no need for a failure path
    printf("find achdi controller failed!\n");
    return 0;
}
