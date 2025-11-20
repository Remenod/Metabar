#include <lib/string.h>

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len] != '\0')
        len++;
    return len;
}

size_t strcpy(char *dst, const char *src)
{
    size_t i = 0;
    while ((dst[i] = src[i]) != '\0')
        i++;
    return i;
}

void strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    while (i < n && src[i] != '\0')
    {
        dst[i] = src[i];
        i++;
    }
    while (i < n)
        dst[i++] = '\0';
}

/* -1 when a < b  |  0 when a == b  |  1 when a > b */
int8_t strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b))
    {
        a++;
        b++;
    }
    if (*(const unsigned char *)a < *(const unsigned char *)b)
        return -1;
    else if (*(const unsigned char *)a > *(const unsigned char *)b)
        return 1;
    else
        return 0;
}

void strcat(char *dst, const char *src)
{
    int dst_len = strlen(dst);
    int i = 0;
    while ((dst[dst_len + i] = src[i]) != '\0')
        i++;
}

char *int_to_str(int64_t value, char *str)
{
    char *p = str;
    char *p1, *p2;
    bool_t is_negative = false;

    if (value < 0)
    {
        is_negative = true;
        value = -value;
    }

    do
    {
        *p++ = (value % 10) + '0';
        value /= 10;
    } while (value);

    if (is_negative)
        *p++ = '-';

    *p = '\0';

    p1 = str;
    p2 = p - 1;
    while (p1 < p2)
    {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }

    return str;
}

char *uint_to_str(uint64_t value, char *str)
{
    char *p = str;
    char *p1, *p2;

    do
    {
        *p++ = (value % 10) + '0';
        value /= 10;
    } while (value);

    *p = '\0';

    p1 = str;
    p2 = p - 1;
    while (p1 < p2)
    {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }

    return str;
}

char *uint_to_str_hex(uint64_t value, char *str)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    char *p = str;
    char *p1, *p2;

    do
    {
        *p++ = hex_digits[value & 0xF];
        value >>= 4;
    } while (value);

    *p = '\0';

    p1 = str;
    p2 = p - 1;
    while (p1 < p2)
    {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }

    return str;
}
