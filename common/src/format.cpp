#include "format.hpp"

// state machine taken from nanobyte_dev (https://www.youtube.com/@nanobyte-dev)

enum printf_state {
    STATE_NORMAL,
    STATE_LENGTH,
    STATE_LENGTH_SHORT,
    STATE_LENGTH_LONG,
    STATE_SPEC
};

enum printf_length {
    LENGTH_DEFAULT,
    LENGTH_SHORT_SHORT,
    LENGTH_SHORT,
    LENGTH_LONG,
    LENGTH_LONG_LONG
};

struct output {
    emit_fn emit;
    void *ctx;
    int count;
};

static void put(struct output *out, char c) {
    out->emit(c, out->ctx);
    out->count++;
}

static void print_unsigned(struct output *out, unsigned long long number, int radix,
                           bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[64];
    int pos = 0;

    // convert number to ascii
    do {
        buffer[pos++] = digits[number % radix];
        number /= radix;
    } while (number > 0);

    // print number in reverse order
    while (--pos >= 0)
        put(out, buffer[pos]);
}

static void print_signed(struct output *out, long long number, int radix, bool upper) {
    if (number < 0) {
        put(out, '-');
        // negate as unsigned so the most negative value doesn't overflow
        unsigned long long unum = static_cast<unsigned long long>(number);
        print_unsigned(out, 0 - unum, radix, upper);
    }
    else
        print_unsigned(out, static_cast<unsigned long long>(number), radix, upper);
}

int vformat(emit_fn emit, void *ctx, const char *fmt, va_list args) {
    struct output out = { emit, ctx, 0 };

    enum printf_state state = STATE_NORMAL;
    enum printf_length length = LENGTH_DEFAULT;
    int radix = 10;
    bool sign = false;
    bool number = false;
    bool upper = false;

    while (*fmt) {
        switch (state) {
            case STATE_NORMAL:
                switch (*fmt) {
                    case '%':   state = STATE_LENGTH;
                                break;
                    default:    put(&out, *fmt);
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
                    // size_t is as wide as long on x86_64
                    case 'z':   length = LENGTH_LONG;
                                state = STATE_SPEC;
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
                    case 'c':   put(&out, (char)va_arg(args, int));
                                break;

                    case 's': {
                                const char *s = va_arg(args, const char *);
                                if (s == nullptr)
                                    s = "(null)";
                                while (*s)
                                    put(&out, *s++);
                                break;
                    }

                    case '%':   put(&out, '%');
                                break;

                    case 'd':
                    case 'i':   radix = 10; sign = true; number = true;
                                break;

                    case 'u':   radix = 10; sign = false; number = true;
                                break;

                    case 'x':   radix = 16; sign = false; number = true;
                                break;

                    case 'X':   radix = 16; sign = false; number = true; upper = true;
                                break;

                    case 'p':   radix = 16; sign = false; number = true;
                                length = LENGTH_LONG_LONG;
                                put(&out, '0'); put(&out, 'x');
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
                        case LENGTH_DEFAULT:     print_signed(&out, va_arg(args, int), radix, upper);
                                                 break;

                        case LENGTH_LONG:        print_signed(&out, va_arg(args, long), radix, upper);
                                                 break;

                        case LENGTH_LONG_LONG:   print_signed(&out, va_arg(args, long long), radix, upper);
                                                 break;
                        }
                    }
                    else {
                        switch (length) {
                        case LENGTH_SHORT_SHORT:
                        case LENGTH_SHORT:
                        case LENGTH_DEFAULT:     print_unsigned(&out, va_arg(args, unsigned int), radix, upper);
                                                 break;

                        case LENGTH_LONG:        print_unsigned(&out, va_arg(args, unsigned long), radix, upper);
                                                 break;

                        case LENGTH_LONG_LONG:   print_unsigned(&out, va_arg(args, unsigned long long), radix, upper);
                                                 break;
                        }
                    }
                }

                // reset state. number has to be reset too, otherwise every %s or %c
                // after a numeric spec would also print a number
                state = STATE_NORMAL;
                length = LENGTH_DEFAULT;
                radix = 10;
                sign = false;
                number = false;
                upper = false;
                break;
        }

        fmt++;
    }

    return out.count;
}

struct snprintf_ctx {
    char *buf;
    size_t size;
    size_t pos;
};

static void snprintf_emit(char c, void *ctx) {
    struct snprintf_ctx *s = static_cast<struct snprintf_ctx *>(ctx);

    // keep the last byte for the NUL, but keep counting so the caller
    // learns how long the full result is
    if (s->pos + 1 < s->size)
        s->buf[s->pos] = c;
    s->pos++;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    struct snprintf_ctx s = { buf, size, 0 };

    vformat(snprintf_emit, &s, fmt, args);

    if (size > 0)
        buf[s.pos < size ? s.pos : size - 1] = '\0';

    return static_cast<int>(s.pos);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}