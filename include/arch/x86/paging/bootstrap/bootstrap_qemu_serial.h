#pragma once

#include <lib/types.h>

uint8_t bootstrap_inb(uint16_t port);
void bootstrap_outb(uint16_t port, uint8_t data);
int bootstrap_serial_is_transmit_empty();
void bootstrap_serial_write_char(char c);
void bootstrap_serial_write_uint8(uint8_t value);
void bootstrap_serial_write_uint32(uint32_t value);
void bootstrap_serial_write_hex_uint8(unsigned char byte);
void bootstrap_serial_write_hex_uint32(uint32_t value);