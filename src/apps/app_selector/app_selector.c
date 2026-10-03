#include <lib/types.h>
#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <lib/string.h>
#include <timer/pit.h>

#include "../snake/snake.h"
#include "../text_sandbox/text_sandbox.h"
#include "../rsod_roulette/rsod_roulette.h"
#include "../segment_test/segment_test.h"
#include "../mouse_playground/mouse_playground.h"
#include "../settings_manager/settings_manager.h"
#include "../expr_eval/expr_eval_main.h"

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
    {"Expression Evaluator", eval_expr_main},
    {"Settings", settings_manager_main},
};

#define APP_COUNT (uint8_t)(sizeof(apps) / sizeof(App))

void app_selector()
{
    while (true)
    {
        set_vga_cursor_visibility(true);
        clear_screen();
        print("=== Application Selector ===\n\n");

        for (uint32_t i = 0; i < APP_COUNT; i++)
        {
            print_dec(i + 1);
            print(". ");
            print(apps[i].name);
            print("\n");
        }

        print("\nPress ESC to exit any app\nSelect app: ");
        uint8_t choice = read_number();
        if (choice > 0 && choice <= APP_COUNT)
        {
            clear_screen();
            apps[choice - 1].entry_point();
        }
        else
        {
            print("\nInvalid selection!\n");
            sleep(1000);
        }
    }
}
