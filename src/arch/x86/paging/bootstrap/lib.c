#include <paging/bootstrap/lib.h>

__attribute__((section(".bootstrap"))) void bootstrap_set_bitmap8_val(uint8_t bitmap[], size_t index, bool_t val)
{
    if (val)
        bitmap[index / 8] |= (0x80 >> (index % 8));
    else
        bitmap[index / 8] &= ~(0x80 >> (index % 8));
}

__attribute__((section(".bootstrap"))) bool_t bootstrap_get_bitmap8_val(uint8_t bitmap[], size_t index)
{
    return (bitmap[index / 8] >> (7 - index % 8)) & 0b1;
}

__attribute__((section(".bootstrap"))) void *bootstrap_memcpy(void *dest, const void *src, uint32_t n)
{
    uint8_t *d = dest;
    const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}
