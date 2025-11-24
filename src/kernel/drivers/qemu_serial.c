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

/* DEC DUMP============ */

static inline void serial_write_dump_uint(const void *dump, size_t count, uint8_t bits)
{
    const uint8_t *ptr = dump;
    size_t step = bits / 8;

    for (size_t i = 0; i < count; i++)
    {
        uint64_t val = 0;

        for (size_t b = 0; b < step; b++)
            val |= ((uint64_t)ptr[i * step + b]) << (b * 8);

        serial_write_uint(val);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

// writes unsigned decimal 8bit number array dump in qemu console
void serial_write_dump_uint8(const uint8_t *dump, size_t count)
{
    serial_write_dump_uint(dump, count, 8);
}

// writes unsigned decimal 16bit number array dump in qemu console
void serial_write_dump_uint16(const uint16_t *dump, size_t count)
{
    serial_write_dump_uint(dump, count, 16);
}

// writes unsigned decimal 32bit number array dump in qemu console
void serial_write_dump_uint32(const uint32_t *dump, size_t count)
{
    serial_write_dump_uint(dump, count, 32);
}

// writes unsigned decimal 64bit number array dump in qemu console
void serial_write_dump_uint64(const uint64_t *dump, size_t count)
{
    serial_write_dump_uint(dump, count, 64);
}

static inline void serial_write_dump_int(const void *dump, size_t count, uint8_t bits)
{
    const uint8_t *ptr = dump;
    size_t step = bits / 8;

    for (size_t i = 0; i < count; i++)
    {
        int64_t val = 0;

        for (size_t b = 0; b < step; b++)
            val |= ((int64_t)ptr[i * step + b]) << (b * 8);

        int64_t sign_mask = (int64_t)1 << (bits - 1);
        if (val & sign_mask)
            val |= (~0ULL << bits);

        serial_write_int(val);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

// writes signed decimal 8bit number array dump in qemu console
void serial_write_dump_int8(const int8_t *dump, size_t count)
{
    serial_write_dump_int(dump, count, 8);
}

// writes signed decimal 16bit number array dump in qemu console
void serial_write_dump_int16(const int16_t *dump, size_t count)
{
    serial_write_dump_int(dump, count, 16);
}

// writes signed decimal 32bit number array dump in qemu console
void serial_write_dump_int32(const int32_t *dump, size_t count)
{
    serial_write_dump_int(dump, count, 32);
}

// writes signed decimal 64bit number array dump in qemu console
void serial_write_dump_int64(const int64_t *dump, size_t count)
{
    serial_write_dump_int(dump, count, 64);
}

/*================BIN================*/

static inline void serial_write_bin(uint64_t value, uint8_t bits)
{
    for (int i = bits - 1; i >= 0; i--)
        serial_write_char((value & ((uint64_t)1 << i)) ? '1' : '0');
}

// writes unsigned binary 8bit number in qemu console
void serial_write_bin_uint8(uint8_t value)
{
    serial_write_bin(value, 8);
}

// writes unsigned binary 16bit number in qemu console
void serial_write_bin_uint16(uint16_t value)
{
    serial_write_bin(value, 16);
}

// writes unsigned binary 32bit number in qemu console
void serial_write_bin_uint32(uint32_t value)
{
    serial_write_bin(value, 32);
}

// writes unsigned binary 64bit number in qemu console
void serial_write_bin_uint64(uint64_t value)
{
    serial_write_bin(value, 64);
}

// writes signed binary 8bit number in qemu console
void serial_write_bin_int8(int8_t value)
{
    serial_write_bin((uint8_t)value, 8);
}

// writes signed binary 16bit number in qemu console
void serial_write_bin_int16(int16_t value)
{
    serial_write_bin((uint16_t)value, 16);
}

// writes signed binary 32bit number in qemu console
void serial_write_bin_int32(int32_t value)
{
    serial_write_bin((uint32_t)value, 32);
}

// writes signed binary 64bit number in qemu console
void serial_write_bin_int64(int64_t value)
{
    serial_write_bin((uint64_t)value, 64);
}

/* BIN DUMP============ */

static inline void serial_write_dump_bin(const void *dump, size_t count, uint8_t bits)
{
    const uint8_t *ptr = dump;
    size_t step = bits / 8;

    for (size_t i = 0; i < count; i++)
    {
        uint64_t val = 0;
        for (size_t b = 0; b < step; b++)
            val |= ((uint64_t)ptr[i * step + b]) << (b * 8);

        serial_write_bin(val, bits);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

// writes unsigned binary 8bit number array dump in qemu console
void serial_write_dump_bin_uint8(const uint8_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 8);
}

// writes unsigned binary 16bit number array dump in qemu console
void serial_write_dump_bin_uint16(const uint16_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 16);
}

// writes unsigned binary 32bit number array dump in qemu console
void serial_write_dump_bin_uint32(const uint32_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 32);
}

// writes unsigned binary 64bit number array dump in qemu console
void serial_write_dump_bin_uint64(const uint64_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 64);
}

// writes signed binary 8bit number array dump in qemu console
void serial_write_dump_bin_int8(const int8_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 8);
}

// writes signed binary 16bit number array dump in qemu console
void serial_write_dump_bin_int16(const int16_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 16);
}

// writes signed binary 32bit number array dump in qemu console
void serial_write_dump_bin_int32(const int32_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 32);
}

// writes signed binary 64bit number array dump in qemu console
void serial_write_dump_bin_int64(const int64_t *dump, size_t count)
{
    serial_write_dump_bin(dump, count, 64);
}

/*===============REAL================*/

// writes float number in qemu console
void serial_write_float(float value)
{
    if (value < 0)
    {
        serial_write_char('-');
        value = -value;
    }

    int int_part = (int)value;
    float frac_part = value - int_part;

    serial_write_uint(int_part);
    serial_write_char('.');

    for (int i = 0; i < 6; i++)
    {
        frac_part *= 10.0f;
        int digit = (int)frac_part;
        serial_write_char('0' + digit);
        frac_part -= digit;
    }
}

// writes double number in qemu console
void serial_write_double(double value)
{
    if (value < 0)
    {
        serial_write_char('-');
        value = -value;
    }

    int64_t int_part = (int64_t)value;
    double frac_part = value - (double)int_part;

    serial_write_uint(int_part);
    serial_write_char('.');

    for (int i = 0; i < 12; i++)
    {
        frac_part *= 10.0;
        int digit = (int)frac_part;
        serial_write_char('0' + digit);
        frac_part -= digit;
    }
}

/* REAL DUMP=========== */

// writes float number array dump in qemu console
void serial_write_dump_float(const float *arr, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        serial_write_float(arr[i]);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

// writes double number array dump in qemu console
void serial_write_dump_double(const double *arr, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        serial_write_double(arr[i]);
        serial_write_char(',');
        serial_write_char(' ');
    }
}

/*===============OTHER===============*/

// writes DAC rgb pallete in qemu console
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
#if 0
void test_serial()
{
    // --- Цілі числа ---
    uint8_t u8 = 42;
    uint16_t u16 = 65535;
    uint32_t u32 = 0xDEADBEEF;
    uint64_t u64 = 0x123456789ABCDEF0ULL;

    int8_t i8 = -42;
    int16_t i16 = -32768;
    int32_t i32 = -123456789;
    int64_t i64 = -0x123456789ABCDEF0LL;

    // --- Hex ---
    serial_write_hex_uint8(u8);
    serial_write_char('\n');
    serial_write_hex_uint16(u16);
    serial_write_char('\n');
    serial_write_hex_uint32(u32);
    serial_write_char('\n');
    serial_write_hex_uint64(u64);
    serial_write_char('\n');

    // --- Binary ---
    serial_write_bin_uint8(u8);
    serial_write_char('\n');
    serial_write_bin_uint16(u16);
    serial_write_char('\n');
    serial_write_bin_uint32(u32);
    serial_write_char('\n');
    serial_write_bin_uint64(u64);
    serial_write_char('\n');

    serial_write_bin_int8(i8);
    serial_write_char('\n');
    serial_write_bin_int16(i16);
    serial_write_char('\n');
    serial_write_bin_int32(i32);
    serial_write_char('\n');
    serial_write_bin_int64(i64);
    serial_write_char('\n');

    // --- Decimal ---
    serial_write_uint(u64);
    serial_write_char('\n');
    serial_write_int(i64);
    serial_write_char('\n');

    // --- Dumps ---
    uint8_t arr8[4] = {1, 2, 3, 255};
    uint16_t arr16[4] = {1000, 2000, 3000, 65535};
    uint32_t arr32[4] = {0x12345678, 0xDEADBEEF, 0, 0xFFFFFFFF};
    uint64_t arr64[4] = {1, 2, 3, 0xFFFFFFFFFFFFFFFFULL};

    int8_t iarr8[4] = {-1, -2, 0, 127};
    int16_t iarr16[4] = {-1000, 0, 1000, 32767};
    int32_t iarr32[4] = {-123456, 0, 123456, 2147483647};
    int64_t iarr64[4] = {-1, 0, 1, 9223372036854775807LL};

    serial_write_dump_hex_uint8(arr8, 4);
    serial_write_char('\n');
    serial_write_dump_hex_uint16(arr16, 4);
    serial_write_char('\n');
    serial_write_dump_hex_uint32(arr32, 4);
    serial_write_char('\n');
    serial_write_dump_hex_uint64(arr64, 4);
    serial_write_char('\n');

    serial_write_dump_uint8(arr8, 4);
    serial_write_char('\n');
    serial_write_dump_uint16(arr16, 4);
    serial_write_char('\n');
    serial_write_dump_uint32(arr32, 4);
    serial_write_char('\n');
    serial_write_dump_uint64(arr64, 4);
    serial_write_char('\n');

    serial_write_dump_int8(iarr8, 4);
    serial_write_char('\n');
    serial_write_dump_int16(iarr16, 4);
    serial_write_char('\n');
    serial_write_dump_int32(iarr32, 4);
    serial_write_char('\n');
    serial_write_dump_int64(iarr64, 4);
    serial_write_char('\n');

    serial_write_dump_bin(arr8, 4, 8);
    serial_write_char('\n');
    serial_write_dump_bin(arr16, 4, 16);
    serial_write_char('\n');
    serial_write_dump_bin(arr32, 4, 32);
    serial_write_char('\n');
    serial_write_dump_bin(arr64, 4, 64);
    serial_write_char('\n');

    // --- Float / Double ---
    float farr[4] = {0.0f, -1.5f, 3.14159f, 123.456f};
    double darr[4] = {0.0, -1.5, 3.1415926535, 123456.789};

    serial_write_float(farr[0]);
    serial_write_char('\n');
    serial_write_double(darr[0]);
    serial_write_char('\n');

    serial_write_dump_float(farr, 4);
    serial_write_char('\n');
    serial_write_dump_double(darr, 4);
    serial_write_char('\n');
}
#endif
