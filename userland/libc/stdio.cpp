#include "stdio.hpp"
#include "../userland.hpp"

#define STDOUT_FD   1
#define OUT_BUF_SIZE 256

struct out_buf {
    char data[OUT_BUF_SIZE];
    size_t len;
};

static void flush(struct out_buf *b) {
    if (b->len > 0) {
        sys_write(STDOUT_FD, b->data, b->len);
        b->len = 0;
    }
}

static void buf_emit(char c, void *ctx) {
    struct out_buf *b = static_cast<struct out_buf *>(ctx);

    if (b->len == OUT_BUF_SIZE)
        flush(b);
    b->data[b->len++] = c;
}

int vprintf(const char *fmt, va_list args) {
    struct out_buf b;
    b.len = 0;

    // one sys_write per OUT_BUF_SIZE characters
    int n = vformat(buf_emit, &b, fmt, args);
    flush(&b);
    return n;
}

int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vprintf(fmt, args);
    va_end(args);
    return n;
}

int putchar(int c) {
    char ch = static_cast<char>(c);
    sys_write(STDOUT_FD, &ch, 1);
    return static_cast<unsigned char>(ch);
}

int puts(const char *s) {
    printf("%s\n", s);
    return 0;
}
