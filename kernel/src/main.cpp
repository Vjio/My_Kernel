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
#include "memory/heap.hpp"
#include "scheduling/scheduler.hpp"
#include "operators.hpp"

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

// global declaration for flanterm pointer
struct flanterm_context *ft_ctx = nullptr;

extern "C" void load_tss();

void trigger_stack_overflow() {
    volatile int garbage[100];
    trigger_stack_overflow();
    garbage[0] = 1;
}

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
    heap_init(hhdm_request.response->offset);

    if (!setup_acpi(rsdp, hhdm_request.response->offset)) {
        printf("APIC setup failed!\n");
        hcf();
    }

    inb(0x60);

    __asm__ volatile ("sti");
    
    printf("Kernel initialized. Enabling interrupts...\n");

    // test for dividing by 0
    // volatile int a = 1;
    // volative int b = 0;
    // volatile inc c = a / b;

    // uncomment this for fun
    // trigger_stack_overflow();

    printf("Hello world!\n");

    Scheduler *scheduler = new Scheduler();
    g_schedulers[get_current_cpu_id()] = scheduler;

    // We're done, just hang...
    hcf();
}
