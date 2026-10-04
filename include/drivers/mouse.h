#pragma once

#include <lib/types.h>

typedef struct
{
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
} mouse_packet_t;

typedef bool_t (*ui_bound_func_t)(uint16_t cursor_x, uint16_t cursor_y, void *ctx);
typedef void (*ui_handler_func_t)(uint16_t cursor_x, uint16_t cursor_y, void *ctx);

typedef struct
{
    void *ctx;
    ui_bound_func_t bound;
    uint8_t handlers_on_release_flags; // &0b001 - left, &0b010 - right, &0b100 - middle
    ui_handler_func_t mouse1_handler;
    ui_handler_func_t mouse2_handler;
    ui_handler_func_t mouse3_handler;
} mouse_ui_element_t;

void register_ui_element(uint8_t layer, mouse_ui_element_t ui_element);

void reset_ui_structure(void);

void reset_ui_layer(uint8_t layer);

void mouse_install(void);

// where the pointer stands, in pixels, for a UI that wants to react before anything is clicked
uint16_t mouse_cursor_x(void);
uint16_t mouse_cursor_y(void);