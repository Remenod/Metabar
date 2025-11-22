#include <drivers/qemu_serial.h>

#include <ports.h>
#include <lib/string.h>

#define COM1_PORT 0x3F8

void serial_init()
{
    outb(COM1_PORT + 1, 0x00); // Disable all interrupts
    outb(COM1_PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud if 115200 base
    outb(COM1_PORT + 1, 0x00); //                  (hi byte)
    outb(COM1_PORT + 3, 0x03); // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

int serial_is_transmit_empty()
{
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_write_char(char c)
{
    while (!serial_is_transmit_empty())
        ;
    outb(COM1_PORT, c);
}

void serial_write_str(const char *s)
{
    while (*s)
        serial_write_char(*s++);
}

void serial_write_hex_uint8(unsigned char byte)
{
    const char hex_digits[] = "0123456789ABCDEF";
    serial_write_char(hex_digits[(byte >> 4) & 0xF]);
    serial_write_char(hex_digits[byte & 0xF]);
}

void serial_write_hex_uint32(uint32_t value)
{
    for (int i = 7; i >= 0; i--)
    {
        int index = (value >> (i * 4)) & 0xF;
        if (index < 10)
            serial_write_uint(index);
        else
            serial_write_char(index + (65 - 10));
    }
}

void serial_write_dump_hex_uint8(const unsigned char *dump, int length)
{
    for (int i = 0; i < length; i++)
    {
        serial_write_char('0');
        serial_write_char('x');
        serial_write_hex_uint8(dump[i]);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

void serial_write_dump_hex_uint32(const unsigned char *dump, int length)
{
    for (int i = 0; i < length; i++)
    {
        serial_write_char('0');
        serial_write_char('x');
        serial_write_hex_uint32(dump[i]);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

// writes unsigned number in qemu console
void serial_write_uint(uint64_t value)
{
    char buf[20];
    int i = 0;

    if (value == 0)
    {
        serial_write_char('0');
        return;
    }

    while (value)
    {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i--)
        serial_write_char(buf[i]);
}

// writes signed number in qemu console
void serial_write_int(int64_t value)
{
    uint64_t u;

    if (value < 0)
    {
        serial_write_char('-');
        u = (uint64_t)(~value) + 1;
    }
    else
    {
        u = (uint64_t)value;
    }

    serial_write_uint(u);
}

void serial_send_palette(uint8_t palette[256][3])
{
    for (int i = 0; i < 256; i++)
    {
        serial_write_char('{');
        for (int j = 0; j < 3; j++)
        {
            serial_write_uint(palette[i][j]);
            if (j < 2)
            {
                serial_write_char(',');
                serial_write_char(' ');
            }
        }
        serial_write_str("},\r\n");
    }
}

void serial_send_font(uint8_t font[256][16])
{
    char print_dec_buf[12];
    for (int i = 0; i < 256; i++)
    {
        serial_write_str(int_to_str(i, print_dec_buf));
        serial_write_str(":  ");
        for (int j = 0; j < 16; j++)
        {

            serial_write_hex_uint8(font[i][j]);
            if (j < 16 - 1)
                serial_write_char(' ');
        }
        serial_write_str("\r\n");
    }
}
