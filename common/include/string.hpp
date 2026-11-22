#pragma once
#include <cstdint>
#include "stddef.h"
#include "memory.hpp"

// compares two strings
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
const char *strstr(const char *haystack, const char *needle);

// finds first occurrence of c (converted to char) in s
const char *strchr(const char *s, int c);

// copies src (including the terminating NUL) into dest
char *strcpy(char *dest, const char *src);
