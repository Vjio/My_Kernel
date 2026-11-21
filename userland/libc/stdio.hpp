#pragma once
#include <stdarg.h>
#include "format.hpp"

// everything here writes to stdout (fd 1)

// writes one character. returns it
int putchar(int c);
// writes the string followed by a newline. returns 0 on success
int puts(const char *s);
// returns the number of characters printed
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list args);
