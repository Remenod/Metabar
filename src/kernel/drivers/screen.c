#include <drivers/screen.h>

#include <ports.h>
#include <lib/string.h>
#include <lib/types.h>

static volatile uint16_t *vga = (volatile uint16_t *)0xC1018000;
static uint16_t cursor_pos = 0;
char print_dec_buf[12];

void put_char(uint16_t pos, unsigned char c)
{
    if (pos < 0 || pos > 1999)
        return;
    vga[pos] = (vga[pos] & 0b1111111100000000) | c;
}

void put_attr(uint16_t pos, uint8_t attr)
{
    vga[pos] = (vga[pos] & 0b0000000011111111) | (attr << 8);
}

void put_attrchar(uint16_t pos, uint16_t attrchar)
{
    vga[pos] = attrchar;
}

unsigned char get_char(uint16_t pos)
{
    return vga[pos] & 0b0000000011111111;
}

uint8_t get_attr(uint16_t pos)
{
    return vga[pos] >> 8;
}

uint16_t get_attrchar(uint16_t pos)
{
    return vga[pos];
}

void set_fg_color(uint16_t pos, uint8_t fg_color)
{
    vga[pos] = (vga[pos] & 0b1111000011111111) | ((fg_color & 0b00001111) << 8);
}

void set_bg_color(uint16_t pos, uint8_t bg_color)
{
    vga[pos] = (vga[pos] & 0b1000111111111111) | ((bg_color & 0b00000111) << 12);
}

void clear_screen()
{
    fill_screen('\0', 0x0F, 0x00);
}

void fill_screen(unsigned char symb, uint8_t fg_color, uint8_t bg_color)
{
    uint16_t fill = ((bg_color << 4 | fg_color) << 8) | symb;
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = fill;

    cursor_pos = 0;
    set_vga_cursor_pos(cursor_pos);
}

void put_string(uint16_t start_pos, const char text[])
{
    int endpoint = start_pos + strlen(text);
    for (int i = start_pos; i < endpoint; i++)
    {
        put_char(i, text[i - start_pos]);
    }
}

void set_vga_cursor_visibility(bool_t visible)
{
    outb(0x3D4, 0x0A);
    uint8_t cursor_start = inb(0x3D5);

    if (visible)
        cursor_start &= ~0x20;
    else
        cursor_start |= 0x20;

    outb(0x3D4, 0x0A);
    outb(0x3D5, cursor_start);
}

void set_vga_cursor_pos(uint16_t pos)
{
    if (pos > 1999)
        pos = 1999;
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    cursor_pos = pos;
}

uint16_t get_vga_cursor_pos(void)
{
    return cursor_pos;
}

void print(const char *text)
{
    for (uint32_t i = 0; i < strlen(text); i++)
        print_char(text[i]);
}

void print_dec(int32_t num)
{
    print(int_to_str(num, print_dec_buf));
}

void print_udec(uint32_t num)
{
    print(uint_to_str(num, print_dec_buf));
}

void print_hex(uint32_t val)
{
    print("0x");
    const char *hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--)
        print_char(hex[(val >> (i * 4)) & 0xF]);
}

void print_bin(uint32_t val, bool_t slicing)
{
    print("0b");
    for (int i = 31; i >= 0; i--)
    {
        print_char(((val >> i) & 1) ? '1' : '0');
        if (slicing && i % 8 == 0 && i != 0)
            print_char(' ');
    }
}

void print_char(char c)
{
    if (c == '\n')
        cursor_pos += 80 - (cursor_pos % 80);
    else if (c == '\b')
        put_char(--cursor_pos, 0);
    else
        put_char(cursor_pos++, c);
    set_vga_cursor_pos(cursor_pos);
}

void scroll_down(void)
{
    cursor_pos -= 80;
    set_vga_cursor_pos(cursor_pos);
    for (uint16_t i = 0; i < 1920; i++)
        vga[i] = vga[i + 80];
    for (uint16_t i = 1920; i < 2000; i++)
        vga[i] &= 0b1111111100000000;
}
