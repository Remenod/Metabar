#pragma once

#include <lib/types.h>

#define contains(arr, size, value) \
    _Generic((value),              \
        uint8_t: contains_uint8,   \
        uint16_t: contains_uint16, \
        uint32_t: contains_uint32, \
        int8_t: contains_int8,     \
        int16_t: contains_int16,   \
        int32_t: contains_int32)(arr, size, value)

bool_t contains_uint8(uint8_t arr[], uint32_t size, uint8_t value);
bool_t contains_uint16(uint16_t arr[], uint32_t size, uint16_t value);
bool_t contains_uint32(uint32_t arr[], uint32_t size, uint32_t value);
bool_t contains_int8(int8_t arr[], uint32_t size, int8_t value);
bool_t contains_int16(int16_t arr[], uint32_t size, int16_t value);
bool_t contains_int32(int32_t arr[], uint32_t size, int32_t value);

void set_bitmap8_val(uint8_t bitmap[], size_t index, bool_t val);
bool_t get_bitmap8_val(uint8_t bitmap[], size_t index);

void set_bitmap64_val(uint64_t bitmap[], size_t index, bool_t val);
bool_t get_bitmap64_val(uint64_t bitmap[], size_t index);