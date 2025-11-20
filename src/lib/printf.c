#include "lib/printf.h"

#include <drivers/screen.h>

typedef enum
{
    MOD_none,
    MOD_hh,
    MOD_h,
    MOD_ll,
    MOD_l,
    MOD_j,
    MOD_z,
    MOD_t,
    MOD_L,
} fmt_length_mod_t;

void printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void vprintf(const char *format, va_list arg)
{
    while (*format)
    {
        char c = *format++;

        if (c != '%')
        {
            print_char(c);
            continue;
        }

        char length_mod = MOD_none;

        switch (*format)
        {
        case 'h':
            length_mod = (*++format == 'h') ? MOD_hh : MOD_h;
            ++format;
            break;
        case 'l':
            length_mod = (*++format == 'l') ? MOD_ll : MOD_l;
            ++format;
            break;
        case 'j':
            length_mod = MOD_j;
            ++format;
            break;
        case 'z':
            length_mod = MOD_z;
            ++format;
            break;
        case 't':
            length_mod = MOD_t;
            ++format;
            break;
        case 'L':
            length_mod = MOD_L;
            ++format;
            break;
        }

        char spec = *format++;

        switch (spec)
        {
        case '%':
            print_char('%');
            break;

        case 'c':
            print_char((unsigned char)va_arg(arg, unsigned int));
            break;

        case 's':
            print(va_arg(arg, const char *));
            break;

        case 'i':
        case 'd':
            switch (length_mod)
            {
            case MOD_none:
                print_dec((int64_t)va_arg(arg, int));
                break;
            case MOD_hh:
                print_dec((int64_t)(signed char)va_arg(arg, int));
                break;
            case MOD_h:
                print_dec((int64_t)(short)va_arg(arg, int));
                break;
            case MOD_ll:
                print_dec((int64_t)va_arg(arg, long long));
                break;
            case MOD_l:
                print_dec((int64_t)va_arg(arg, long));
                break;
            case MOD_j:
                print_dec((int64_t)va_arg(arg, intmax_t));
                break;
            case MOD_z:
                print_dec((int64_t)va_arg(arg, size_t));
                break;
            case MOD_t:
                print_dec((int64_t)va_arg(arg, ptrdiff_t));
                break;
            case MOD_L:
                print("(inval_arg_spec)");
                break;
            }
            break;

        case 'X':
        case 'x':
            switch (length_mod)
            {
            case MOD_none:
                print_hex((uint64_t)va_arg(arg, unsigned int));
                break;
            case MOD_hh:
                print_hex((uint64_t)(unsigned char)va_arg(arg, int));
                break;
            case MOD_h:
                print_hex((uint64_t)(unsigned short)va_arg(arg, int));
                break;
            case MOD_ll:
                print_hex((uint64_t)va_arg(arg, unsigned long long));
                break;
            case MOD_l:
                print_hex((uint64_t)va_arg(arg, unsigned long));
                break;
            case MOD_j:
                print_hex((uint64_t)va_arg(arg, uintmax_t));
                break;
            case MOD_z:
                print_hex((uint64_t)va_arg(arg, size_t));
                break;
            case MOD_t:
                print_hex((uint64_t)va_arg(arg, size_t));
                break;
            case MOD_L:
                print("(inval_arg_spec)");
                break;
            }
            break;

        case 'u':
            switch (length_mod)
            {
            case MOD_none:
                print_udec((uint64_t)va_arg(arg, unsigned int));
                break;
            case MOD_hh:
                print_udec((uint64_t)(unsigned char)va_arg(arg, int));
                break;
            case MOD_h:
                print_udec((uint64_t)(unsigned short)va_arg(arg, int));
                break;
            case MOD_ll:
                print_udec((uint64_t)va_arg(arg, unsigned long long));
                break;
            case MOD_l:
                print_udec((uint64_t)va_arg(arg, unsigned long));
                break;
            case MOD_j:
                print_udec((uint64_t)va_arg(arg, uintmax_t));
                break;
            case MOD_z:
                print_udec((uint64_t)va_arg(arg, size_t));
                break;
            case MOD_t:
                print_udec((uint64_t)va_arg(arg, size_t));
                break;
            case MOD_L:
                print("(inval_arg_spec)");
                break;
            }
            break;

        case 'p':
            print_hex((uint32_t)va_arg(arg, void *));
            break;

        case 'o':
        case 'F':
        case 'f':
        case 'E':
        case 'e':
        case 'A':
        case 'a':
        case 'G':
        case 'g':
        case 'n':
            print("(unsupported_spec)");
            break;

        default:
            print("(inval_arg)");
            break;
        }
    }
}
