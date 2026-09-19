#pragma once
#include <cstdint>
#include "stddef.h"
#include "./memory/memory.hpp"

// compares two strings
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
const char *strstr(const char *haystack, const char *needle);
