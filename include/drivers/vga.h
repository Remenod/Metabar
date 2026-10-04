#pragma once

#include <lib/types.h>

#define FONT_HEIGHT 16

void set_graphics_mode(void);
void set_text_mode(void);
void draw_mode13h_test_pattern(void);

const uint8_t *get_8x16_font_glyph(uint8_t glyph_code);

void vga_read_colour(uint8_t colour, uint8_t rgb[3]);
void vga_write_colour(uint8_t colour, const uint8_t rgb[3]);

void vga_set_blink(bool_t on);

void write_font(const uint8_t font[256][FONT_HEIGHT]);
void write_glyphs(uint8_t glyphs_count, const uint8_t glyphs[glyphs_count][FONT_HEIGHT], const uint8_t glyph_codes[glyphs_count]);
void write_glyph(const uint8_t glyph[FONT_HEIGHT], uint8_t glyph_code);