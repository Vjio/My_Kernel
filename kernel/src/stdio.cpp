#include "stdio.hpp"
#include <cstdarg>
#include "flanterm/flanterm.h"
#include "flanterm/flanterm_backends/fb.h"

extern struct flanterm_context *ft_ctx;

void putc(char c) {
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

const char g_HexChars[] = "0123456789abcdef";

static void printf_unsigned(unsigned long long number, int radix) {
    char buffer[64];
    int pos = 0;

    // convert number to ascii
    do {
        unsigned long long rem = number % radix;
        number /= radix;
        buffer[pos++] = g_HexChars[rem];
    } while (number > 0);

    // print number in reverse order
    while (--pos >= 0)
        putc(buffer[pos]);
}

static void printf_signed(long long number, int radix) {
    if (number < 0) {
        putc('-');
        // negate
        unsigned long long unum = static_cast<unsigned long long>(number);
        printf_unsigned(0 - unum, radix);
    }
    else 
        printf_unsigned(static_cast<unsigned long long>(number), radix);
}

// taken from nanobyte_dev (https://www.youtube.com/@nanobyte-dev)
void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    enum printf_state state = STATE_NORMAL;
    enum printf_length length = LENGTH_DEFAULT;
    int radix = 10;
    bool sign = false;
    bool number = false;

    while (*fmt) {
        switch (state) {
            case STATE_NORMAL:
                switch (*fmt) {
                    case '%':   state = STATE_LENGTH;
                                break;
                    default:    putc(*fmt);
                                break;
                }
                break;

            case STATE_LENGTH:
                switch (*fmt) {
                    case 'h':   length = LENGTH_SHORT;
                                state = STATE_LENGTH_SHORT;
                                break;
                    case 'l':   length = LENGTH_LONG;
                                state = STATE_LENGTH_LONG;
                                break;
                    default:    goto STATE_SPEC_LABEL;
                }
                break;

            case STATE_LENGTH_SHORT:
                if (*fmt == 'h') {
                    length = LENGTH_SHORT_SHORT;
                    state = STATE_SPEC;
                }
                else goto STATE_SPEC_LABEL;
                break;

            case STATE_LENGTH_LONG:
                if (*fmt == 'l') {
                    length = LENGTH_LONG_LONG;
                    state = STATE_SPEC;
                }
                else goto STATE_SPEC_LABEL;
                break;

            case STATE_SPEC:
            STATE_SPEC_LABEL:
                switch (*fmt) {
                    case 'c':   putc((char)va_arg(args, int));
                                break;

                    case 's':   
                                puts(va_arg(args, const char*));
                                break;

                    case '%':   putc('%');
                                break;

                    case 'd':
                    case 'i':   radix = 10; sign = true; number = true;
                                break;

                    case 'u':   radix = 10; sign = false; number = true;
                                break;

                    case 'X':
                    case 'x':
                    case 'p':   radix = 16; sign = false; number = true;
                                break;

                    case 'o':   radix = 8; sign = false; number = true;
                                break;

                    // ignore invalid spec
                    default:    break;
                }

                if (number) {
                    if (sign) {
                        switch (length) {
                        case LENGTH_SHORT_SHORT:
                        case LENGTH_SHORT:
                        case LENGTH_DEFAULT:     printf_signed(va_arg(args, int), radix);
                                                        break;

                        case LENGTH_LONG:        printf_signed(va_arg(args, long), radix);
                                                        break;

                        case LENGTH_LONG_LONG:   printf_signed(va_arg(args, long long), radix);
                                                        break;
                        }
                    }
                    else {
                        switch (length) {
                        case LENGTH_SHORT_SHORT:
                        case LENGTH_SHORT:
                        case LENGTH_DEFAULT:     printf_unsigned(va_arg(args, unsigned int), radix);
                                                        break;
                                                        
                        case LENGTH_LONG:        printf_unsigned(va_arg(args, unsigned  long), radix);
                                                        break;

                        case LENGTH_LONG_LONG:   printf_unsigned(va_arg(args, unsigned  long long), radix);
                                                        break;
                        }
                    }
                }

                // reset state
                state = STATE_NORMAL;
                length = LENGTH_DEFAULT;
                radix = 10;
                sign = false;
                break;
        }

        fmt++;
    }

    va_end(args);
}
