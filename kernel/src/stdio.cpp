#include "stdio.hpp"
#include <cstdarg>
#include "flanterm/flanterm.h"
#include "flanterm/flanterm_backends/fb.h"
#include "serial.hpp"
#include "format.hpp"

extern struct flanterm_context *ft_ctx;

void putc(char c) {
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);

    if (ft_ctx != nullptr) {
        // intercept \n
        if (c == '\n') {
            char cr = '\r';
            flanterm_write(ft_ctx, &cr, 1);
        }

        flanterm_write(ft_ctx, &c, 1);
    }
}

void puts(const char* s) {
    size_t len = 0;
    while (s[len]) 
        len++;
    if (ft_ctx != nullptr)
        flanterm_write(ft_ctx, s, len);
}

static void kernel_emit(char c, void *) {
    putc(c);
}

void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags));

    vformat(kernel_emit, nullptr, fmt, args);

    va_end(args);
    if (flags & (1u << 9))
        __asm__ volatile("sti");
}
