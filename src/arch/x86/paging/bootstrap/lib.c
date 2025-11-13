#include <paging/bootstrap/lib.h>

// __attribute__((section(".bootstrap"))) uint8_t bootstrap_inb(uint16_t port)
// {
//     uint8_t ret;
//     asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
//     return ret;
// }
// __attribute__((section(".bootstrap"))) void bootstrap_outb(uint16_t port, uint8_t data)
// {
//     asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
// }
// __attribute__((section(".bootstrap"))) int bootstrap_serial_is_transmit_empty()
// {
//     return bootstrap_inb(0x3F8 + 5) & 0x20;
// }
// __attribute__((section(".bootstrap"))) void bootstrap_serial_write_char(char c)
// {
//     while (!bootstrap_serial_is_transmit_empty())
//         ;
//     bootstrap_outb(0x3F8, c);
// }
// __attribute__((section(".bootstrap"))) void bootstrap_serial_write_uint8(uint8_t value)
// {
//     char buf[4];
//     int i = 0;
//
//     if (value == 0)
//     {
//         bootstrap_serial_write_char('0');
//         return;
//     }
//
//     while (value > 0)
//     {
//         buf[i++] = '0' + (value % 10);
//         value /= 10;
//     }
//
//     while (i--)
//         bootstrap_serial_write_char(buf[i]);
// }
// __attribute__((section(".bootstrap"))) void bootstrap_serial_write_uint32(uint32_t value)
// {
//     char buf[12];
//     int i = 0;
//
//     if (value == 0)
//     {
//         bootstrap_serial_write_char('0');
//         return;
//     }
//
//     while (value > 0)
//     {
//         buf[i++] = '0' + (value % 10);
//         value /= 10;
//     }
//
//     while (i--)
//         bootstrap_serial_write_char(buf[i]);
// }
// __attribute__((section(".bootstrap"))) void bootstrap_serial_write_hex_uint8(unsigned char byte)
// {
//     int index = (byte >> 4) & 0xF;
//     for (int i = 0; i < 2; i++)
//     {
//         if (index < 10)
//             bootstrap_serial_write_uint32(index);
//         else
//             bootstrap_serial_write_char(index + (65 - 10));
//         index = byte & 0xF;
//     }
// }
// __attribute__((section(".bootstrap"))) void bootstrap_serial_write_hex_uint32(uint32_t value)
// {
//     for (int i = 7; i >= 0; i--)
//     {
//         // bootstrap_serial_write_char(hex_digits[0]);
//         int index = (value >> (i * 4)) & 0xF;
//         if (index < 10)
//             bootstrap_serial_write_uint32(index);
//         else
//             bootstrap_serial_write_char(index + (65 - 10));
//     }
// }

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
