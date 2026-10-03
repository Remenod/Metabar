#include <lib/string.h>

uint32_t strlen(const char *str)
{
    int len = 0;
    while (str[len] != '\0')
        len++;
    return len;
}

uint32_t strcpy(char *dst, const char *src)
{
    int i = 0;
    while ((dst[i] = src[i]) != '\0')
        i++;
    return i;
}

void strncpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i = 0;
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

char *int_to_str(int32_t value, char *str)
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

char *uint_to_str(uint32_t value, char *str)
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

char *uint_to_str_hex(uint32_t value, char *str)
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

int str_to_int(const char *s)
{
    int n = 0;
    bool_t neg = false;
    while (is_space(*s))
        s++;
    switch (*s)
    {
    case '-':
        neg = true;
    case '+':
        s++;
    }
    /* Compute n as a negative number to avoid overflow on INT_MIN */
    while (is_digit(*s))
        n = 10 * n - (*s++ - '0');
    return neg ? n : -n;
}

#pragma GCC diagnostic pop