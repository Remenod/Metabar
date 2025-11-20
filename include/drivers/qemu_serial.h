#pragma once

#include <lib/types.h>

void serial_init();

int serial_is_transmit_empty();

void serial_write_char(char c);

void serial_write_str(const char *s);

void serial_write_hex_byte(unsigned char byte);

void serial_write_hex_uint32(uint32_t value);

void serial_write_dump_hex(const unsigned char *dump, int length);

void serial_write_uint8(uint8_t value);

void serial_write_uint16(uint16_t value);

void serial_write_uint32(uint32_t value);

void serial_send_palette(uint8_t palette[256][3]);

void serial_send_font(uint8_t font[256][16]);
