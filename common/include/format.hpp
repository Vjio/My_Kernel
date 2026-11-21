#pragma once
#include <stdarg.h>
#include <stddef.h>

// receives one formatted character at a time
typedef void (*emit_fn)(char c, void *ctx);

// the formatting core behind printf. it does no I/O itself
// every character goes through emit, so the same code can print to the screen,
// to a file descriptor or into a buffer.
//
// supported: %c %s %d %i %u %x %X %o %p %%
// length modifiers: hh h l ll z
// not supported (yet): width, precision, padding, flags
//
// returns the number of characters emitted
int vformat(emit_fn emit, void *ctx, const char *fmt, va_list args);

// writes at most size - 1 characters into buf and always NUL terminates (if size > 0)
// returns the length the full result would have had, like C99 snprintf
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);