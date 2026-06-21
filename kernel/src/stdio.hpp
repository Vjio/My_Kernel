#pragma once

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

// prints a character to screen
void putc(char c);
// prints a string to screen
void puts(const char* s);
// format and print data
void printf(const char* fmt, ...);
