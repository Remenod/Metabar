#include "expr_eval.h"

#include <lib/string.h>
#include <drivers/keyboard.h>
#include <drivers/screen.h>
#include <kernel/diagnostics/rsod_routine.h>

#define SCREEN_WIDTH 80
#define STACK_SIZE 32

static void print_error(char const* msg)
{
    for (int j = 80; j < 160; j++)
        put_char(j, 0);
    put_string(80, "error: ");
    put_string(87, msg);
}

static void display()
{
    
}

void eval_expr_main(void)
{
    set_vga_cursor_visibility(true);
    set_vga_cursor_pos(0);
    char buf[SCREEN_WIDTH];
    int buf_idx = 0;
    int stack[STACK_SIZE];

    token_t actions[STACK_SIZE];
    size_t actions_count = 0;
    size_t action_index = 0;
    bool_t exploration_mode = false;

    while (true)
    {
        char c;
        while (!(c = get_keyboard_char()))
            asm volatile("hlt");

        switch (c)
        {
        case KEY_ESC:
            return;

        case KEY_LEFT:
            if (exploration_mode && action_index > 0)
            {
                action_index--;
            }
            break;

        case KEY_RIGHT:
            if (exploration_mode && action_index < actions_count)
            {
                action_index++;
                display();
            }
            break;

        case '\b':
            if (exploration_mode)
            {
                exploration_mode = false;
            }
            else
            {
                if (buf_idx > 0 && get_vga_cursor_pos() > 0)
                    buf_idx--;
                set_vga_cursor_pos(get_vga_cursor_pos() - 1);
                print_char('\0');
                set_vga_cursor_pos(get_vga_cursor_pos() - 1);
            }
            break;

        case '\n':
        {
            buf[buf_idx] = '\0';
            size_t n = eval_expr(buf, stack, STACK_SIZE, print_error);
            for (size_t i = 0; i < n; ++i)
            {
                char buf[12];
                for (int j = 80; j < 160; j++)
                put_char(j, 0);
                put_string(80, int_to_str(stack[i], buf));
            }
            exploration_mode = true;
        }
        break;

        default:
            print_char(c);
            buf[buf_idx++] = c;
            break;
        }
    }
}
