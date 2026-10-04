#include <lib/types.h>
#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <lib/string.h>

#include "../snake/snake.h"
#include "../text_sandbox/text_sandbox.h"
#include "../rsod_roulette/rsod_roulette.h"
#include "../segment_test/segment_test.h"
#include "../mouse_playground/mouse_playground.h"
#include "../settings_manager/settings_manager.h"

#define SCREEN_WIDTH 80
#define GLYPH_WIDTH 8
#define GLYPH_HEIGHT 16

#define TITLE_ROW 2
#define FIRST_ROW 6
#define ROW_STEP 2 // a blank line between items, so that a highlighted one stands on its own
#define HINT_ROW 21
#define ITEM_LEFT 20
#define ITEM_WIDTH 40

typedef struct
{
    const char *name;
    func_t entry_point;
} App;

static App apps[] = {
    {"Text Sandbox", text_sandbox_main},
    {"Snake", snake_main},
    {"RSoD Roulette", rsod_roulette_main},
    {"Segment Test", segment_test_main},
    {"Mouse Playground", mouse_playground_main},
    {"Settings", settings_manager_main},
};

#define APP_COUNT (uint8_t)(sizeof(apps) / sizeof(App))

static uint8_t slots[APP_COUNT];      // what a registered element is handed, so it knows its own row
static volatile uint8_t selected = 0; // the mouse handler writes both of these from an interrupt
static volatile bool_t launch = false;

static uint16_t item_pos(uint8_t index)
{
    return (uint16_t)((FIRST_ROW + index * ROW_STEP) * SCREEN_WIDTH + ITEM_LEFT);
}

// the chosen item is the one written the other way round, which is all a menu has to say
static void paint_item(uint8_t index, bool_t chosen)
{
    const uint16_t at = item_pos(index);

    for (uint16_t i = 0; i < ITEM_WIDTH; i++)
    {
        set_bg_color(at + i, chosen ? LIGHT_GREY : BLACK);
        set_fg_color(at + i, chosen ? BLACK : LIGHT_GREY);
    }
}

static void draw_item(uint8_t index)
{
    char line[ITEM_WIDTH + 1];
    char number[12];
    uint32_t length = 0;

    uint_to_str(index + 1, number);

    line[length++] = ' ';
    line[length++] = ' ';
    for (uint32_t i = 0; number[i] != '\0'; i++)
        line[length++] = number[i];
    line[length++] = '.';
    line[length++] = ' ';

    for (uint32_t i = 0; apps[index].name[i] != '\0' && length < ITEM_WIDTH; i++)
        line[length++] = apps[index].name[i];

    while (length < ITEM_WIDTH) // the whole width is painted, so the highlight is a bar and not a word
        line[length++] = ' ';

    line[length] = '\0';
    put_string(item_pos(index), line);
}

static void select_item(uint8_t index)
{
    if (index >= APP_COUNT)
        return;

    paint_item(selected, false);
    selected = index;
    paint_item(selected, true);
}

static void put_centered(uint8_t row, const char *text)
{
    put_string((uint16_t)(row * SCREEN_WIDTH + (SCREEN_WIDTH - strlen(text)) / 2), text);
}

static bool_t item_bound(uint16_t x, uint16_t y, void *ctx)
{
    const uint16_t row = (uint16_t)(FIRST_ROW + *(const uint8_t *)ctx * ROW_STEP);

    return x >= ITEM_LEFT * GLYPH_WIDTH && x < (ITEM_LEFT + ITEM_WIDTH) * GLYPH_WIDTH &&
           y >= row * GLYPH_HEIGHT && y < (row + 1) * GLYPH_HEIGHT;
}

static void item_click(uint16_t x, uint16_t y, void *ctx)
{
    (void)x;
    (void)y;

    selected = *(const uint8_t *)ctx; // drawing is left to the loop: this runs in the mouse interrupt
    launch = true;
}

static void draw_menu(void)
{
    set_vga_cursor_visibility(false);
    clear_screen();
    put_centered(TITLE_ROW, "=== Application Selector ===");

    reset_ui_structure();

    for (uint8_t i = 0; i < APP_COUNT; i++)
    {
        slots[i] = i;
        draw_item(i);
        paint_item(i, i == selected);

        register_ui_element(i, (mouse_ui_element_t){
                                   .ctx = &slots[i],
                                   .handlers_on_release_flags = 0b001, // a menu acts when the button comes back up
                                   .bound = item_bound,
                                   .mouse1_handler = item_click,
                                   .mouse2_handler = (ui_handler_func_t)NULL,
                                   .mouse3_handler = (ui_handler_func_t)NULL,
                               });
    }

    put_centered(HINT_ROW, "Up and Down choose, Enter runs it, or click one");
    put_centered(HINT_ROW + 1, "Esc leaves any app");
}

void app_selector()
{
    while (true)
    {
        draw_menu();
        launch = false;

        while (!launch)
        {
            const char key = get_keyboard_char();

            if (key == 0)
            {
                asm volatile("hlt");
                continue;
            }

            if (key >= '1' && key <= '9')
                select_item((uint8_t)(key - '1'));
            else if (key == KEY_UP && selected > 0)
                select_item((uint8_t)(selected - 1));
            else if (key == KEY_DOWN)
                select_item((uint8_t)(selected + 1));
            else if (key == '\n' || key == ' ')
                launch = true;
        }

        reset_ui_structure(); // from here the screen and the mouse belong to the app
        set_vga_cursor_visibility(true);
        clear_screen();
        apps[selected].entry_point();
    }
}
