#include "atto.h"

#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <kernel/fat32.h>
#include <kernel/memory.h>
#include <lib/mem.h>
#include <lib/string.h>

#define ATTO_MAX 32768 // the whole file is held in memory, so it has to have a limit
#define COLUMNS 80
#define ROWS 24 // the last line of the screen belongs to the status bar
#define STATUS (ROWS * COLUMNS)
#define STATUS_ATTR ((LIGHT_GREY << 4) | BLACK)

static char *text;
static uint32_t length;
static uint32_t cursor;
static uint32_t scroll; // the first row of the text that is on screen
static bool_t modified;

/* Walks the text once, giving every character the row and the column it takes up, and draws the
 * ones that fall inside the window when asked to. A line longer than the screen wraps onto the
 * next row rather than being cut off. */
static void layout(bool_t draw, uint32_t *cursor_row, uint32_t *cursor_col)
{
    uint32_t row = 0;
    uint32_t col = 0;

    for (uint32_t i = 0; i <= length; i++)
    {
        if (i == cursor)
        {
            *cursor_row = row;
            *cursor_col = col;
        }

        if (i == length)
            return;

        if (text[i] == '\n')
        {
            row++;
            col = 0;
            continue;
        }

        if (draw && row >= scroll && row < scroll + ROWS)
            put_char((uint16_t)((row - scroll) * COLUMNS + col), (unsigned char)text[i]);

        if (++col == COLUMNS)
        {
            col = 0;
            row++;
        }
    }
}

static void status(const char *path, const char *message)
{
    char number[12];

    for (uint16_t i = 0; i < COLUMNS; i++)
    {
        put_char(STATUS + i, ' ');
        put_attr(STATUS + i, STATUS_ATTR);
    }

    put_string(STATUS + 1, path);
    uint_to_str(length, number);
    put_string(STATUS + 34, number);
    put_string(STATUS + 34 + strlen(number) + 1, "bytes");

    if (modified)
        put_string(STATUS + 46, "modified"); // the size can reach "32768 bytes", which ends at 44

    put_string(STATUS + 55, message); // and the hint is as wide as the line has room for
}

/* A question needs the whole status line: put at the right hand end, like a hint, it would run
 * off the screen and be cut in half. */
static void prompt(const char *question)
{
    for (uint16_t i = 0; i < COLUMNS; i++)
    {
        put_char(STATUS + i, ' ');
        put_attr(STATUS + i, STATUS_ATTR);
    }

    put_string(STATUS + 1, question);
}

static void refresh(const char *path, const char *message)
{
    uint32_t row = 0;
    uint32_t col = 0;

    layout(false, &row, &col); // where the cursor landed decides what part of the text is shown

    if (row < scroll)
        scroll = row;
    else if (row >= scroll + ROWS)
        scroll = row - ROWS + 1;

    fill_screen(' ', LIGHT_GREY, BLACK);
    layout(true, &row, &col);
    status(path, message);
    set_vga_cursor_pos((uint16_t)((row - scroll) * COLUMNS + col));
}

static void insert(char c)
{
    if (length + 1 >= ATTO_MAX)
        return;

    memmove(text + cursor + 1, text + cursor, length - cursor);
    text[cursor++] = c;
    length++;
    modified = true;
}

static void erase_range(uint32_t from, uint32_t to)
{
    if (from >= to)
        return;

    memmove(text + from, text + to, length - to);
    length -= to - from;
    cursor = from;
    modified = true;
}

// the start of the word before the cursor, which is what ctrl takes back; the line end stops it
static uint32_t word_start(void)
{
    uint32_t at = cursor;

    while (at > 0 && text[at - 1] == ' ')
        at--;

    while (at > 0 && text[at - 1] != ' ' && text[at - 1] != '\n')
        at--;

    return at;
}

static uint32_t line_start(uint32_t at)
{
    while (at > 0 && text[at - 1] != '\n')
        at--;

    return at;
}

static uint32_t line_end(uint32_t at)
{
    while (at < length && text[at] != '\n')
        at++;

    return at;
}

// up and down keep the column inside the line above or below, or stop at its end when it is shorter
static void move_line(bool_t down)
{
    const uint32_t start = line_start(cursor);
    const uint32_t column = cursor - start;
    uint32_t target;

    if (down)
    {
        const uint32_t end = line_end(cursor);
        if (end == length)
            return;
        target = end + 1;
    }
    else
    {
        if (start == 0)
            return;
        target = line_start(start - 1);
    }

    const uint32_t end = line_end(target);
    cursor = target + column < end ? target + column : end;
}

static const char *save(const char *path)
{
    if (fat32_write_file(path, text, length) != length)
        return "could not save";

    modified = false;
    return "saved";
}

static char wait_key(void)
{
    char c = 0;

    while (!(c = get_keyboard_char()))
        asm volatile("hlt");

    return c;
}

void atto_main(const char *path)
{
    fat32_entry_t entry;
    const char *message = "^S save ^X exit Esc menu";

    length = 0;
    cursor = 0;
    scroll = 0;
    modified = false;

    if (fat32_stat(path, &entry) && (entry.is_dir || entry.size > ATTO_MAX))
    {
        print(entry.is_dir ? "atto: that is a directory\n" : "atto: the file is larger than 32 KiB\n");
        return;
    }

    text = malloc(ATTO_MAX);
    if (text == NULL)
    {
        print("atto: no memory for the buffer\n");
        return;
    }

    if (fat32_stat(path, &entry))
        length = fat32_read_file(path, text, ATTO_MAX);

    set_vga_cursor_visibility(true);

    for (;;)
    {
        refresh(path, message);
        message = "^S save ^X exit Esc menu";

        const char key = wait_key();

        if (key == KEY_CTRL_S)
        {
            message = save(path);
            continue;
        }

        /* Leaving with something unsaved is the one thing worth stopping for, so ctrl x asks, and
         * goes straight out when there is nothing to lose. */
        if (key == KEY_CTRL_X)
        {
            if (!modified)
                break;

            prompt("leave: s = save and go   q = go anyway   anything else = back");

            const char choice = wait_key();

            if (choice == 'q')
                break;

            if (choice == 's')
            {
                message = save(path);
                if (!modified)
                    break;
            }

            continue;
        }

        if (key == KEY_ESC)
        {
            prompt("s = save   q = quit   anything else = back");

            const char choice = wait_key();

            if (choice == 'q')
                break;

            if (choice == 's')
                message = save(path);

            continue;
        }

        switch (key)
        {
        case '\b':
            if (cursor > 0)
                erase_range(cursor - 1, cursor);
            break;
        case KEY_DELETE:
            if (cursor < length)
                erase_range(cursor, cursor + 1);
            break;
        case KEY_ERASE_WORD:
            erase_range(word_start(), cursor);
            break;
        case KEY_HOME:
            cursor = line_start(cursor);
            break;
        case KEY_END:
            cursor = line_end(cursor);
            break;
        case '\t':
            for (uint32_t i = 0; i < 4; i++)
                insert(' ');
            break;
        case KEY_LEFT:
            if (cursor > 0)
                cursor--;
            break;
        case KEY_RIGHT:
            if (cursor < length)
                cursor++;
            break;
        case KEY_UP:
            move_line(false);
            break;
        case KEY_DOWN:
            move_line(true);
            break;
        default:
            if (key == '\n' || key >= ' ')
                insert(key);
            break;
        }
    }

    free(text);
    clear_screen();
}
