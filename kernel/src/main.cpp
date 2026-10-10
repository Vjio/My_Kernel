#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <limine.h>
#include "flanterm/flanterm.h"
#include "flanterm/flanterm_backends/fb.h"
#include "stdio.hpp"
#include "gdt.hpp"
#include "interrupts/idt.hpp"
#include "interrupts/tss.hpp"
#include "interrupts/pic.hpp"
#include "io.hpp"
#include "interrupts/acpi.hpp"
#include "memory/pmm.hpp"
#include "memory/vmm.hpp"
#include "memory/heap.hpp"
#include "memory/memory.hpp"
#include "scheduling/scheduler.hpp"
#include "operators.hpp"
#include "scheduling/process.hpp"
#include "interrupts/pci.hpp"
#include "file_system/ahci.hpp"
#include "file_system/drive.hpp"
#include "file_system/sata.hpp"
#include "file_system/file.hpp"

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

namespace {

__attribute__((used, section(".limine_requests")))
volatile std::uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

}

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

namespace {

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

// root system description pointer
__attribute__((used, section(".limine_requests")))
volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

}

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .cpp file, as seen fit.

namespace {

__attribute__((used, section(".limine_requests_start")))
volatile std::uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
volatile std::uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

}

// Halt and catch fire function.
namespace {

void hcf() {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}

}

// The following stubs are required by the Itanium C++ ABI (the one we use,
// regardless of the "Itanium" nomenclature).
// Like the memory functions above, these stubs can be moved to a different .cpp file,
// but should not be removed, unless you know what you are doing.
extern "C" {
    int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
    void __cxa_pure_virtual() { hcf(); }
    void *__dso_handle;
}

// Extern declarations for global constructors array.
extern void (*__init_array[])();
extern void (*__init_array_end[])();

// GLOBALS

struct flanterm_context *ft_ctx = nullptr;

// this struct is only used to establish the kernel's struct before initializing its heap
// (so as to circumvent a "chicken and egg" type of problem)
// if you ever need to interact with the kernel process struct, ask the scheduler for it
// (scheduler::get_running_thread). do not use this struct!
struct process kernel_process;

// END GLOBALS

extern "C" void load_tss();

void trigger_stack_overflow() {
    volatile int garbage[100];
    trigger_stack_overflow();
    garbage[0] = 1;
}

// void test_scratch_drive(SATADrive *drive) {
//     const uint32_t test_sizes[] = { 1, 8, 32, 33, 64, 50, 200 };
//     uint64_t lba = 1000;

//     for (uint32_t count : test_sizes) {
//         printf("about to write count=%u\n", count);
//         size_t bytes = count * drive->get_sector_size();
//         uint8_t *write_buf = reinterpret_cast<uint8_t*>(malloc(bytes));
//         uint8_t *read_buf  = reinterpret_cast<uint8_t*>(malloc(bytes));

//         for (size_t i = 0; i < bytes; i++)
//             write_buf[i] = (uint8_t)(i ^ 0xA5);
//         memset(read_buf, 0, bytes);

//         bool write_ok = drive->write_sectors(lba, count, write_buf);
//         bool read_ok  = drive->read_sectors(lba, count, read_buf);
//         bool match    = write_ok && read_ok && (memcmp(write_buf, read_buf, bytes) == 0);

//         printf("count=%u write=%d read=%d match=%d\n", count, write_ok, read_ok, match);

//         free(write_buf);
//         free(read_buf);
//         lba += count + 16;
//     }
//     printf("done!\n");
// }

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
extern "C" void kmain() {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Call global constructors.
    for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++) {
        __init_array[i]();
    }

    // Ensure limine requests have been answered
    if (framebuffer_request.response == nullptr
     || framebuffer_request.response->framebuffer_count < 1) {
        printf("Limine could not find the framebuffer!\n");
        hcf();
    }

    if (rsdp_request.response == nullptr || rsdp_request.response->address == nullptr) {
        printf("Limine could not find the ACPI RSDP!\n");
        hcf(); 
    }

    if (hhdm_request.response == nullptr) {
        printf("Limine HHDM response missing!\n");
        hcf(); 
    }

    if (memmap_request.response == nullptr) {
        printf("Limine memmap response missing!\n");
        hcf(); 
    }

    // fetch limine's responses
    limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    struct RSDP2 *rsdp = reinterpret_cast<struct RSDP2 *>(rsdp_request.response->address);

    // Print a nice pattern to screen as an example.
    // Note: we assume the framebuffer model is RGB with 32-bit pixels.
    // volatile std::uint32_t *fb_ptr = static_cast<volatile std::uint32_t *>(framebuffer->address);
    // for (std::size_t y = 0; y < framebuffer->height; y++) {
    //     for (std::size_t x = 0; x < framebuffer->width; x++) {
    //         std::uint32_t nX = x * 255 / framebuffer->width;
    //         std::uint32_t nY = y * 255 / framebuffer->height;
    //         fb_ptr[y * (framebuffer->pitch / 4) + x] = (nY << 8) | nX;
    //     }
    // }

    // for now, i will use flanterm to emulate the terminal
    // i don't particularly wish to have to deal with every pixel in the framebuffer
    ft_ctx = flanterm_fb_init(
        nullptr, // malloc func (defaults to a safe internal allocator if null)
        nullptr, // free func
        static_cast<std::uint32_t *>(framebuffer->address),
        framebuffer->width,
        framebuffer->height,
        framebuffer->pitch,
        framebuffer->red_mask_size,
        framebuffer->red_mask_shift,
        framebuffer->green_mask_size,
        framebuffer->green_mask_shift,
        framebuffer->blue_mask_size,
        framebuffer->blue_mask_shift,
        nullptr, nullptr, // default canvas colors
        nullptr, nullptr, // default text colors
        nullptr, nullptr, // default ansi colors
        nullptr, // default font
        nullptr, // default font bold
        NULL,    // default font spacing
        0, 0, 1, // antialiasing/metrics
        0, 0,    // margins
        0        // fallback
    );

    tss_init();
    setup_gdt();
    load_tss();
    setup_idt();
    PIC_disable();

    PMM::init_PMM(memmap_request.response, hhdm_request.response->offset);
    VMM::init(hhdm_request.response->offset);

    populate_kernel_process_struct(&kernel_process);
    heap_init(hhdm_request.response->offset, &kernel_process);

    if (!setup_acpi(rsdp, hhdm_request.response->offset)) {
        printf("APIC setup failed!\n");
        hcf();
    }

    inb(0x60);

    // test for dividing by 0
    // volatile int a = 1;
    // volative int b = 0;
    // volatile inc c = a / b;

    // uncomment this for fun
    // trigger_stack_overflow();

    // inits scheduler per core singleton
    Scheduler::get_current_scheduler();

    // create_user_test_process();
    // Scheduler::get_current_scheduler()->get_running_thread()->thread_sleep(1000);

    // per core things that the kernel will need:
    // - per core TSS
    // - per core scheduler

    printf("Kernel initialized! Interrups are now enabled\n");
    ahci_init(find_ahci_controller(hhdm_request.response->offset), hhdm_request.response->offset);

    __asm__ volatile ("sti");
    // test_scratch_drive(static_cast<SATADrive*>(Drive::drives[0]));

    printf("mounting file system\n");
    FileSystem *fs = VSFS::mount_file_system(Drive::drives[0]);
    struct thread *kernel_thread = Scheduler::get_current_scheduler()->get_running_thread();
    kernel_thread->parent->fs = fs;
    kernel_thread->parent->cwd = fs->inode_get(ROOT_INODE);
    printf("file system mounted successfully\n");

    // TODO: once stdin, stdout etc are added to all programs
    // add it to kernel program too

    // We're done, just hang...
    hcf();
}
