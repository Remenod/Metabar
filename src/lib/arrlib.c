#include <lib/arrlib.h>

bool_t contains_uint8(uint8_t arr[], uint32_t size, uint8_t value)
{
    for (uint32_t i = 0; i < size; i++)
        if (arr[i] == value)
            return true;
    return false;
}

bool_t contains_uint16(uint16_t arr[], uint32_t size, uint16_t value)
{
    for (uint32_t i = 0; i < size; i++)
        if (arr[i] == value)
            return true;
    return false;
}

bool_t contains_uint32(uint32_t arr[], uint32_t size, uint32_t value)
{
    for (uint32_t i = 0; i < size; i++)
        if (arr[i] == value)
            return true;
    return false;
}

bool_t contains_int8(int8_t arr[], uint32_t size, int8_t value)
{
    for (uint32_t i = 0; i < size; i++)
        if (arr[i] == value)
            return true;
    return false;
}

bool_t contains_int16(int16_t arr[], uint32_t size, int16_t value)
{
    for (uint32_t i = 0; i < size; i++)
        if (arr[i] == value)
            return true;
    return false;
}

bool_t contains_int32(int32_t arr[], uint32_t size, int32_t value)
{
    for (uint32_t i = 0; i < size; i++)
        if (arr[i] == value)
            return true;
    return false;
}

void set_bitmap8_val(uint8_t bitmap[], size_t index, bool_t val)
{
    if (val)
        bitmap[index / 8] |= (0x80 >> (index % 8));
    else
        bitmap[index / 8] &= ~(0x80 >> (index % 8));
}

bool_t get_bitmap8_val(uint8_t bitmap[], size_t index)
{
    return (bitmap[index / 8] >> (7 - index % 8)) & 0b1;
}

void set_bitmap64_val(uint64_t bitmap[], size_t index, bool_t val)
{
    if (val)
        bitmap[index / 64] |= (0x8000000000000000 >> (index % 64));
    else
        bitmap[index / 64] &= ~(0x8000000000000000 >> (index % 64));
}

bool_t get_bitmap64_val(uint64_t bitmap[], size_t index)
{
    return (bitmap[index / 64] >> (63 - index % 64)) & 0b1;
}