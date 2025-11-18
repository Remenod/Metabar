#include <drivers/mouse.h>

#include <interrupts/isr.h>
#include <interrupts/pic.h>
#include <drivers/screen.h>
#include <drivers/vga.h>
#include <ports.h>
#include <lib/string.h>
#include <lib/mem.h>
#include <lib/math_generic.h>
#include <kernel/settings.h>

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT 0x64

// #define MOUSE_CHAR_1 0xA6
// #define MOUSE_CHAR_2 0xA7
// #define MOUSE_CHAR_3 0xEC
// #define MOUSE_CHAR_4 0xED
#define MOUSE_CHAR_1 0xC0
#define MOUSE_CHAR_2 0xC1
#define MOUSE_CHAR_3 0xC2
#define MOUSE_CHAR_4 0xC3

static uint16_t mouse_x = 0, mouse_y = 0;

static mouse_packet_t last_packet;
static uint8_t cursor_cover_buf[4], packets_buf[3], mouse_packet_index = 0;

static const uint8_t mouse_glyphs_codes[] = {MOUSE_CHAR_1, MOUSE_CHAR_2, MOUSE_CHAR_3, MOUSE_CHAR_4};

static mouse_ui_element_t ui_elements[256] = {0};
static uint8_t highest_ui_layer = 0;

static uint16_t mouse_sensitivity;
static bool_t mouse_debug_info;

static const uint8_t arrow_glyph[] = {
    0b10000000,
    0b11000000,
    0b11100000,
    0b11110000,
    0b11111000,
    0b11111100,
    0b11111110,
    0b11111110,
    0b11110000,
    0b11011000,
    0b10011000,
    0b00001100,
    0b00001100,
    0b00000000,
    0b00000000,
    0b00000000};

static const uint8_t arrow_mask_pressed[] = {
    0b00000000,
    0b00000000,
    0b01000000,
    0b01100000,
    0b01110000,
    0b01111000,
    0b01111100,
    0b01100000,
    0b01000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000};

static const uint8_t select_glyph[] = {
    0b10000000,
    0b11000000,
    0b11100000,
    0b11110000,
    0b11111000,
    0b11111100,
    0b11111110,
    0b11111110,
    0b11100000,
    0b11000000,
    0b10000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000,
    0b00000000};

static const uint8_t *cursor_glyph = arrow_glyph;

static inline bool_t is_mouse1(uint8_t buttons)
{
    return buttons & 0b001;
}
static inline bool_t is_mouse2(uint8_t buttons)
{
    return buttons & 0b010;
}
static inline bool_t is_mouse3(uint8_t buttons)
{
    return buttons & 0b100;
}

static inline void get_covered_chars_pos(uint16_t dest[4], uint16_t mouse_x, uint16_t mouse_y)
{
    uint16_t base_pos = (mouse_y / 16) * 80 + (mouse_x / 8);
    dest[0] = base_pos;
    dest[1] = base_pos + 1;
    dest[2] = base_pos + 80;
    dest[3] = base_pos + 81;
}

static void cursor_process(void)
{
    uint16_t covered_chars_pos[4];
    get_covered_chars_pos(covered_chars_pos, mouse_x, mouse_y);

    for (int i = 0; i < 4; i++)
        if (!(get_char(covered_chars_pos[i]) - mouse_glyphs_codes[i]))
            put_char(covered_chars_pos[i], cursor_cover_buf[i]);

    int new_x = mouse_x + round((last_packet.dx * (mouse_sensitivity / 100.0f)));
    int new_y = mouse_y - round((last_packet.dy * (mouse_sensitivity / 100.0f)));

    new_x = new_x < 0 ? 0 : new_x;
    mouse_x = new_x > (80 * 8 - 1) ? (80 * 8 - 1) : new_x;
    new_y = new_y < 0 ? 0 : new_y;
    mouse_y = new_y > (25 * 16 - 1) ? (25 * 16 - 1) : new_y;

    if (mouse_debug_info)
    {
        char mouse_debug_buf[12];
        put_string(1999 - 79, "y:   ");
        put_string(1999 - 77, uint_to_str(mouse_y, mouse_debug_buf));
        put_string(1999 - 79 - 80, "x:   ");
        put_string(1999 - 77 - 80, uint_to_str(mouse_x, mouse_debug_buf));
    }

    get_covered_chars_pos(covered_chars_pos, mouse_x, mouse_y);

    uint8_t mouse_glyph_buf[4][16] = {0};

    uint8_t cur_ch_offset_x = mouse_x % 8;
    uint8_t cur_ch_offset_y = mouse_y % 16;

    for (int i = 0; i < 4; i++)
    {
        const uint8_t *covering_glyph = get_8x16_font_glyph(get_char(covered_chars_pos[i]));

        uint8_t cur_gl_offset_y;

        for (int j = 0; j < 16; j++)
        {
            cur_gl_offset_y = j - cur_ch_offset_y + (i < 2 ? 0 : 16);
            mouse_glyph_buf[i][j] =
                covering_glyph[j] | ((cur_gl_offset_y > 15) // Since cur_gl_offset_y is unsigned, values below 0 wrap to large numbers (~255)
                                         ? 0
                                         : (i % 2
                                                ? (cursor_glyph[cur_gl_offset_y] << (8 - cur_ch_offset_x))
                                                : (cursor_glyph[cur_gl_offset_y] >> cur_ch_offset_x)));
            if (last_packet.buttons)
                mouse_glyph_buf[i][j] ^=
                    ((cur_gl_offset_y > 15) // Since cur_gl_offset_y is unsigned, values below 0 wrap to large numbers (~255)
                         ? 0
                         : (i % 2
                                ? (arrow_mask_pressed[cur_gl_offset_y] << (8 - cur_ch_offset_x))
                                : (arrow_mask_pressed[cur_gl_offset_y] >> cur_ch_offset_x)));
        }
    }
    write_glyphs(4, mouse_glyph_buf, mouse_glyphs_codes);

    for (int i = 0; i < 4; i++)
    {
        cursor_cover_buf[i] = get_char(covered_chars_pos[i]);
        if (mouse_x >= (79 * 8) && i % 2)
            continue;
        put_char(covered_chars_pos[i], mouse_glyphs_codes[i]);
    }
}

static void click_process(uint8_t prev_buttons)
{
    int selected = -1;

    for (int i = highest_ui_layer; i >= 0; i--)
    {
        if (ui_elements[i].bound)
        {
            if (ui_elements[i].bound(mouse_x, mouse_y, ui_elements[i].ctx))
            {
                cursor_glyph = select_glyph;
                selected = i;
                if (i > highest_ui_layer)
                    highest_ui_layer = i;
                break;
            }
        }
    }
    if (selected == -1)
    {
        cursor_glyph = arrow_glyph;
        return;
    }

    mouse_ui_element_t *el = &ui_elements[selected];

    if (el->mouse1_handler && ((is_mouse1(prev_buttons) && !is_mouse1(last_packet.buttons)) ||
                               (is_mouse1(last_packet.buttons) && !is_mouse1(el->handlers_on_release_flags))))
        el->mouse1_handler(mouse_x, mouse_y, el->ctx);

    if (el->mouse2_handler && ((is_mouse2(prev_buttons) && !is_mouse2(last_packet.buttons)) ||
                               (is_mouse2(last_packet.buttons) && !is_mouse2(el->handlers_on_release_flags))))
        el->mouse2_handler(mouse_x, mouse_y, el->ctx);

    if (el->mouse3_handler && ((is_mouse3(prev_buttons) && !is_mouse3(last_packet.buttons)) ||
                               (is_mouse3(last_packet.buttons) && !is_mouse3(el->handlers_on_release_flags))))
        el->mouse3_handler(mouse_x, mouse_y, el->ctx);
}

static void sensitivity_subscriber(int new_value)
{
    if (new_value < 0)
        new_value = 0;

    mouse_sensitivity = new_value;
}

static void debug_info_subscriber(int new_value)
{
    for (int i = 0; i < 6; i++)
    {
        put_char(1999 - 79 + i, 0);
        put_char(1999 - 79 - 80 + i, 0);
    }
    mouse_debug_info = new_value != 0;
}

static inline void ps2_wait_input_empty(void)
{
    while (inb(PS2_STATUS_PORT) & 0x02)
        ;
}

static inline bool_t ps2_wait_output_full_timeout(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(PS2_STATUS_PORT) & 0x01)
            return true;
    return false;
}

static void ps2_write_cmd(uint8_t cmd)
{
    ps2_wait_input_empty();
    outb(PS2_CMD_PORT, cmd);
}

static void ps2_write_data(uint8_t data)
{
    ps2_wait_input_empty();
    outb(PS2_DATA_PORT, data);
}

static uint8_t ps2_read_data(void)
{
    if (!ps2_wait_output_full_timeout())
        return 0xFF;
    return inb(PS2_DATA_PORT);
}

static void ps2_mouse_write(uint8_t data)
{
    ps2_write_cmd(0xD4);
    ps2_write_data(data);
    uint8_t ack = ps2_read_data();
    (void)ack;
}

void register_ui_element(uint8_t layer, mouse_ui_element_t ui_element)
{
    highest_ui_layer = highest_ui_layer > layer ? highest_ui_layer : layer;
    ui_elements[layer] = ui_element;
}

void reset_ui_structure(void)
{
    memset(&ui_elements, 0, sizeof(ui_elements));
    highest_ui_layer = 0;
}

void reset_ui_layer(uint8_t layer)
{
    ui_elements[layer].bound = NULL;
}

void mouse_handler(void)
{
    uint8_t data = inb(PS2_DATA_PORT);

    packets_buf[mouse_packet_index++] = data;

    if (mouse_packet_index == 3)
    {
        mouse_packet_index = 0;

        uint8_t prev_buttons_state = last_packet.buttons;

        last_packet = (mouse_packet_t){
            .buttons = packets_buf[0] & 0b111, // &0b001 - left, &0b010 - right, &0b100 - middle
            .dx = (int8_t)packets_buf[1],
            .dy = (int8_t)packets_buf[2]};

        click_process(prev_buttons_state);
        cursor_process();
    }
}

void mouse_install(void)
{
    asm volatile("sti"); // intended

    pic_unmask_irq(2);
    pic_unmask_irq(12);

    // Turn on the second PS/2 port (mouse)
    ps2_write_cmd(0xA8);

    // Read the config (command byte)
    ps2_write_cmd(0x20);
    uint8_t status = ps2_read_data();

    // Set bits: enable IRQ12 (bits 0=keyboard, 1=mouse)
    status |= 0x02;  // enable mouse IRQ
    status &= ~0x20; // disable translation (required on real hardware)

    ps2_write_cmd(0x60);
    ps2_write_data(status);

    ps2_mouse_write(0xFF); // reset
    ps2_read_data();       // ACK 0xFA
    ps2_read_data();       // self-test pass = 0xAA
    ps2_read_data();       // mouse ID (0x00 for standard)

    ps2_mouse_write(0xF6); // set defaults
    ps2_mouse_write(0xF4); // enable streaming
    reset_ui_structure();

    sensitivity_subscriber(settings_get_int("mouse.sensitivity", 100));
    debug_info_subscriber(settings_get_int("mouse.debug_info", false));
    settings_subscribe("mouse.sensitivity", sensitivity_subscriber);
    settings_subscribe("mouse.debug_info", debug_info_subscriber);

    register_interrupt_handler(44, mouse_handler);
}
