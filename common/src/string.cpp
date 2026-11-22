#include "string.hpp"

int strncmp(const char *s1, const char *s2, size_t n) {
    unsigned char u1, u2;

    while (n-- > 0) {
        u1 = (unsigned char) *s1++;
        u2 = (unsigned char) *s2++;
        if (u1 != u2)
            return u1 - u2;
        if (u1 == '\0')
            return 0;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p)
        ++p;
    return static_cast<size_t>(p - s);
}

const char *strstr(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0)
        return haystack;

    for (; *haystack; ++haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, n) == 0)
            return haystack;
    }

    return nullptr;
}

int strcmp(const char *s1, const char *s2) {
    unsigned char u1, u2;

    do {
        u1 = (unsigned char) *s1++;
        u2 = (unsigned char) *s2++;
        if (u1 != u2)
            return u1 - u2;
    } while (u1 != '\0');

    return 0;
}

const char *strchr(const char *s, int c) {
    const char ch = static_cast<char>(c);

    for (;; ++s) {
        if (*s == ch)
            return s;
        if (*s == '\0')
            return nullptr;
    }
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0')
        ;
    return dest;
}
