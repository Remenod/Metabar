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
#include "../shell/shell.h"

#define SCREEN_WIDTH 80
#define GLYPH_WIDTH 8
#define GLYPH_HEIGHT 16

#define TITLE_ROW 2
#define FIRST_ROW 6
#define LAST_ROW 18
#define ROW_STEP 2 // a blank line between items, so that a highlighted one stands on its own
#define PAGE_ROW 20
#define HINT_ROW 21
#define ITEM_LEFT 20
#define ITEM_WIDTH 40
#define BUTTON_WIDTH 5 // the width of "[ < ]"
#define BUTTON_GAP 3

#define ITEMS_PER_PAGE (uint8_t)((LAST_ROW - FIRST_ROW) / ROW_STEP + 1)

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
    {"Shell", shell_main},
};

#define APP_COUNT (uint8_t)(sizeof(apps) / sizeof(App))
#define PAGE_COUNT (uint8_t)((APP_COUNT + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE)

static uint8_t slots[ITEMS_PER_PAGE]; // one per row on screen, holding the app it stands for
static uint8_t page = 0;
static volatile uint8_t selected = 0; // the mouse handler writes both of these from an interrupt
static volatile bool_t launch = false;
static uint16_t pointer_x = 0;           // where the pointer was last seen, so that a pointer which
static uint16_t pointer_y = 0;           // merely rests somewhere does not fight the arrow keys
static uint8_t buttons[2] = {0, 1};      // ctx of the two page buttons, which is how they tell each other apart
static uint16_t button_col[2];           // where they ended up, which depends on how wide the page line is
static volatile int8_t page_request = 0; // set by a click on one of them, acted on by the loop

static uint8_t page_base(void)
{
    return (uint8_t)(page * ITEMS_PER_PAGE);
}

// the last page is usually not a full one
static uint8_t page_rows(void)
{
    const uint8_t left = (uint8_t)(APP_COUNT - page_base());

    return left < ITEMS_PER_PAGE ? left : ITEMS_PER_PAGE;
}

static uint8_t row_of(const void *ctx)
{
    return (uint8_t)((const uint8_t *)ctx - slots);
}

static uint16_t item_pos(uint8_t row)
{
    return (uint16_t)((FIRST_ROW + row * ROW_STEP) * SCREEN_WIDTH + ITEM_LEFT);
}

// the chosen item is the one written the other way round, which is all a menu has to say
static void paint_row(uint8_t row, bool_t chosen)
{
    const uint16_t at = item_pos(row);

    for (uint16_t i = 0; i < ITEM_WIDTH; i++)
    {
        set_bg_color(at + i, chosen ? LIGHT_GREY : BLACK);
        set_fg_color(at + i, chosen ? BLACK : LIGHT_GREY);
    }
}

static void draw_row(uint8_t row)
{
    char line[ITEM_WIDTH + 1];
    char number[12];
    uint32_t length = 0;

    uint_to_str(row + 1, number); // numbered by what is on screen, so a key matches what is seen

    line[length++] = ' ';
    line[length++] = ' ';
    for (uint32_t i = 0; number[i] != '\0'; i++)
        line[length++] = number[i];
    line[length++] = '.';
    line[length++] = ' ';

    for (uint32_t i = 0; apps[slots[row]].name[i] != '\0' && length < ITEM_WIDTH; i++)
        line[length++] = apps[slots[row]].name[i];

    while (length < ITEM_WIDTH) // the whole width is painted, so the highlight is a bar and not a word
        line[length++] = ' ';

    line[length] = '\0';
    put_string(item_pos(row), line);
}

static void put_centered(uint8_t row, const char *text)
{
    put_string((uint16_t)(row * SCREEN_WIDTH + (SCREEN_WIDTH - strlen(text)) / 2), text);
}

static bool_t inside_row(uint16_t x, uint16_t y, uint8_t row)
{
    const uint16_t top = (uint16_t)(FIRST_ROW + row * ROW_STEP);

    return x >= ITEM_LEFT * GLYPH_WIDTH && x < (ITEM_LEFT + ITEM_WIDTH) * GLYPH_WIDTH &&
           y >= top * GLYPH_HEIGHT && y < (top + 1) * GLYPH_HEIGHT;
}

static bool_t item_bound(uint16_t x, uint16_t y, void *ctx)
{
    return inside_row(x, y, row_of(ctx));
}

static void item_click(uint16_t x, uint16_t y, void *ctx)
{
    (void)x;
    (void)y;

    selected = *(const uint8_t *)ctx; // drawing is left to the loop: this runs in the mouse interrupt
    launch = true;
}

static bool_t page_button_bound(uint16_t x, uint16_t y, void *ctx)
{
    const uint8_t which = *(const uint8_t *)ctx;

    return x >= button_col[which] * GLYPH_WIDTH && x < (button_col[which] + BUTTON_WIDTH) * GLYPH_WIDTH &&
           y >= PAGE_ROW * GLYPH_HEIGHT && y < (PAGE_ROW + 1) * GLYPH_HEIGHT;
}

static void page_button_click(uint16_t x, uint16_t y, void *ctx)
{
    (void)x;
    (void)y;

    // turning a page redraws the screen, which is no work for an interrupt: the loop takes it from here
    page_request = *(const uint8_t *)ctx == 0 ? -1 : 1;
}

// a button that leads nowhere is dimmed rather than taken away, so that the line keeps its shape
static void draw_page_button(uint8_t which, bool_t usable)
{
    const uint16_t at = (uint16_t)(PAGE_ROW * SCREEN_WIDTH + button_col[which]);

    put_string(at, which == 0 ? "[ < ]" : "[ > ]");

    for (uint16_t i = 0; i < BUTTON_WIDTH; i++)
        set_fg_color(at + i, usable ? WHITE : DARK_GREY);
}

static void draw_page(void)
{
    char number[12];

    set_vga_cursor_visibility(false);
    clear_screen();
    put_centered(TITLE_ROW, "=== Application Selector ===");

    reset_ui_structure();

    for (uint8_t row = 0; row < page_rows(); row++)
    {
        slots[row] = (uint8_t)(page_base() + row);
        draw_row(row);
        paint_row(row, slots[row] == selected);

        register_ui_element(row, (mouse_ui_element_t){
                                     .ctx = &slots[row],
                                     .handlers_on_release_flags = 0b001, // a menu acts when the button comes up
                                     .bound = item_bound,
                                     .mouse1_handler = item_click,
                                     .mouse2_handler = (ui_handler_func_t)NULL,
                                     .mouse3_handler = (ui_handler_func_t)NULL,
                                 });
    }

    if (PAGE_COUNT > 1)
    {
        char line[32] = "Page ";

        uint_to_str(page + 1, number);
        strcat(line, number);
        strcat(line, " of ");
        uint_to_str(PAGE_COUNT, number);
        strcat(line, number);

        const uint32_t width = BUTTON_WIDTH + BUTTON_GAP + strlen(line) + BUTTON_GAP + BUTTON_WIDTH;
        const uint16_t left = (uint16_t)((SCREEN_WIDTH - width) / 2);

        button_col[0] = left;
        button_col[1] = (uint16_t)(left + width - BUTTON_WIDTH);

        put_string((uint16_t)(PAGE_ROW * SCREEN_WIDTH + left + BUTTON_WIDTH + BUTTON_GAP), line);
        draw_page_button(0, page > 0);
        draw_page_button(1, page + 1 < PAGE_COUNT);

        for (uint8_t which = 0; which < 2; which++)
            register_ui_element((uint8_t)(ITEMS_PER_PAGE + which),
                                (mouse_ui_element_t){
                                    .ctx = &buttons[which],
                                    .handlers_on_release_flags = 0b001,
                                    .bound = page_button_bound,
                                    .mouse1_handler = page_button_click,
                                    .mouse2_handler = (ui_handler_func_t)NULL,
                                    .mouse3_handler = (ui_handler_func_t)NULL,
                                });
    }

    put_centered(HINT_ROW, "Up and Down choose, Enter runs it, or click one");
    put_centered(HINT_ROW + 1, "Esc leaves any app");

    // a pointer that has not moved since is left alone, so that it cannot take the choice over
    pointer_x = mouse_cursor_x();
    pointer_y = mouse_cursor_y();
}

// takes an app by its place in the whole list, turning the page when it is not on this one
static void choose(uint8_t index)
{
    if (index >= APP_COUNT)
        return;

    if (index < page_base() || index >= page_base() + page_rows())
    {
        selected = index;
        page = (uint8_t)(index / ITEMS_PER_PAGE);
        draw_page();
        return;
    }

    paint_row((uint8_t)(selected - page_base()), false);
    selected = index;
    paint_row((uint8_t)(selected - page_base()), true);
}

static void turn_page(bool_t forward)
{
    if (forward && page + 1 < PAGE_COUNT)
        page++;
    else if (!forward && page > 0)
        page--;
    else
        return;

    selected = page_base();
    draw_page();
}

/* The pointer carries the choice with it, but only while it is moving: left standing on an item it
 * would otherwise take every choice away from the arrow keys. */
static void follow_pointer(void)
{
    const uint16_t x = mouse_cursor_x();
    const uint16_t y = mouse_cursor_y();

    if (x == pointer_x && y == pointer_y)
        return;

    pointer_x = x;
    pointer_y = y;

    for (uint8_t row = 0; row < page_rows(); row++)
        if (inside_row(x, y, row))
        {
            choose(slots[row]);
            return;
        }
}

void app_selector()
{
    while (true)
    {
        draw_page();
        launch = false;

        while (!launch)
        {
            if (page_request != 0)
            {
                const bool_t forward = page_request > 0;

                page_request = 0;
                turn_page(forward);
                continue;
            }

            const char key = get_keyboard_char();

            if (key == 0)
            {
                asm volatile("hlt"); // the next interrupt is a key, a tick, or the mouse moving
                follow_pointer();
                continue;
            }

            if (key >= '1' && key <= '9' && (uint8_t)(key - '1') < page_rows())
                choose((uint8_t)(page_base() + (key - '1')));
            else if (key == KEY_UP && selected > 0)
                choose((uint8_t)(selected - 1));
            else if (key == KEY_DOWN)
                choose((uint8_t)(selected + 1));
            else if (key == '[')
                turn_page(false);
            else if (key == ']')
                turn_page(true);
            else if (key == '\n' || key == ' ')
                launch = true;
        }

        reset_ui_structure(); // from here the screen and the mouse belong to the app
        set_vga_cursor_visibility(true);
        clear_screen();
        apps[selected].entry_point();
    }
}
