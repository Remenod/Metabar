#pragma once

#include <lib/types.h>

size_t strlen(const char *str);

char *int_to_str(int64_t value, char *str);

char *uint_to_str(uint64_t value, char *str);

char *uint_to_str_hex(uint64_t value, char *str);

size_t strcpy(char *dst, const char *src);

void strncpy(char *dst, const char *src, size_t n);

int8_t strcmp(const char *a, const char *b);

void strcat(char *dst, const char *src);