#include <drivers/qemu_serial.h>

#include <ports.h>
#include <lib/string.h>

#define COM1_PORT 0x3F8

static const char hex_digits[] = "0123456789ABCDEF";

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

// writes single char in qemu console
void serial_write_char(char c)
{
    while (!serial_is_transmit_empty())
        ;
    outb(COM1_PORT, c);
}

// writes string in qemu console
void serial_write_str(const char *s)
{
    while (*s)
        serial_write_char(*s++);
}

/*================HEX================*/

static inline void serial_write_hex(uint64_t value, uint8_t bits)
{
    uint8_t nibbles = bits / 4;

    for (int i = nibbles - 1; i >= 0; i--)
        serial_write_char(hex_digits[(value >> (i * 4)) & 0xF]);
}

// writes unsigned hex 8bit number in qemu console
void serial_write_hex_uint8(uint8_t value)
{
    serial_write_hex(value, 8);
}

// writes unsigned hex 16bit number in qemu console
void serial_write_hex_uint16(uint16_t value)
{
    serial_write_hex(value, 16);
}

// writes unsigned hex 32bit number in qemu console
void serial_write_hex_uint32(uint32_t value)
{
    serial_write_hex(value, 32);
}

// writes unsigned hex 64bit number in qemu console
void serial_write_hex_uint64(uint64_t value)
{
    serial_write_hex(value, 64);
}

/* HEX DUMP============ */

static inline void serial_write_dump_hex(const void *dump, size_t count, uint8_t bits)
{
    const uint8_t *ptr = dump;

    for (size_t i = 0; i < count; i++)
    {
        serial_write_char('0');
        serial_write_char('x');

        uint64_t val = 0;
        for (int b = 0; b < bits / 8; b++)
            val |= ((uint64_t)ptr[i * (bits / 8) + b]) << (b * 8);

        serial_write_hex(val, bits);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

// writes unsigned hex 8bit number array dump in qemu console
void serial_write_dump_hex_uint8(const uint8_t *dump, size_t count)
{
    serial_write_dump_hex(dump, count, 8);
}

// writes unsigned hex 16bit number array dump in qemu console
void serial_write_dump_hex_uint16(const uint16_t *dump, size_t count)
{
    serial_write_dump_hex(dump, count, 16);
}

// writes unsigned hex 32bit number array dump in qemu console
void serial_write_dump_hex_uint32(const uint32_t *dump, size_t count)
{
    serial_write_dump_hex(dump, count, 32);
}

// writes unsigned hex 64bit number array dump in qemu console
void serial_write_dump_hex_uint64(const uint64_t *dump, size_t count)
{
    serial_write_dump_hex(dump, count, 64);
}

/*================DEC================*/

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

// writes VGA 03h mode font in qemu console
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
