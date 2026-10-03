#pragma once

#include <lib/types.h>

uint32_t strlen(const char *str);

char *int_to_str(int32_t value, char *str);

char *uint_to_str(uint32_t value, char *str);

char *uint_to_str_hex(uint32_t value, char *str);

uint32_t strcpy(char *dst, const char *src);

void strncpy(char *dst, const char *src, uint32_t n);

int8_t strcmp(const char *a, const char *b);

void strcat(char *dst, const char *src);

int str_to_int(const char *s);

inline bool_t is_space(char c)
{
    return (c == '\t' || c == '\n' || c == '\f' || c == ' ');
}

inline bool_t is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

inline bool_t is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
