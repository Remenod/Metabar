#pragma once

#include <lib/types.h>

void serial_init();

int serial_is_transmit_empty();

void serial_write_char(char c);

void serial_write_str(const char *s);

void serial_write_hex_uint8(uint8_t byte);
void serial_write_hex_uint16(uint16_t byte);
void serial_write_hex_uint32(uint32_t value);
void serial_write_hex_uint64(uint64_t value);

void serial_write_dump_hex_uint8(const uint8_t *dump, size_t count);
void serial_write_dump_hex_uint16(const uint16_t *dump, size_t count);
void serial_write_dump_hex_uint32(const uint32_t *dump, size_t count);
void serial_write_dump_hex_uint64(const uint64_t *dump, size_t count);

void serial_write_uint(uint64_t value);

void serial_write_int(int64_t value);

void serial_write_dump_uint8(const uint8_t *dump, size_t count);
void serial_write_dump_uint16(const uint16_t *dump, size_t count);
void serial_write_dump_uint32(const uint32_t *dump, size_t count);
void serial_write_dump_uint64(const uint64_t *dump, size_t count);

void serial_write_dump_int8(const int8_t *dump, size_t count);
void serial_write_dump_int16(const int16_t *dump, size_t count);
void serial_write_dump_int32(const int32_t *dump, size_t count);
void serial_write_dump_int64(const int64_t *dump, size_t count);

void serial_write_bin_uint8(uint8_t value);
void serial_write_bin_uint16(uint16_t value);
void serial_write_bin_uint32(uint32_t value);
void serial_write_bin_uint64(uint64_t value);

void serial_write_bin_int8(int8_t value);
void serial_write_bin_int16(int16_t value);
void serial_write_bin_int32(int32_t value);
void serial_write_bin_int64(int64_t value);

void serial_write_dump_uint8(const uint8_t *dump, size_t count);
void serial_write_dump_uint16(const uint16_t *dump, size_t count);
void serial_write_dump_uint32(const uint32_t *dump, size_t count);
void serial_write_dump_uint64(const uint64_t *dump, size_t count);

void serial_write_dump_int8(const int8_t *dump, size_t count);
void serial_write_dump_int16(const int16_t *dump, size_t count);
void serial_write_dump_int32(const int32_t *dump, size_t count);
void serial_write_dump_int64(const int64_t *dump, size_t count);

void serial_send_palette(uint8_t palette[256][3]);

void serial_send_font(uint8_t font[256][16]);
