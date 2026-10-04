#include "screensaver.h"

#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <drivers/speaker.h>
#include <drivers/vga.h>
#include <timer/pit.h>
#include <kernel/fat32.h>
#include <kernel/memory.h>
#include <lib/random.h>

#define SCREEN_COLUMNS 80
#define SCREEN_ROWS 25
#define SCREEN_CELLS (SCREEN_COLUMNS * SCREEN_ROWS)

#define HALF_BLOCK 223
#define WIDTH SCREEN_COLUMNS
#define HEIGHT (SCREEN_ROWS * 2)
#define FUEL_ROW (HEIGHT - 1)

#define SHADES 16 // the whole palette becomes one ramp from black through red to white
#define FRAMES_PER_SECOND 50

typedef struct
{
    uint16_t hz;
    uint16_t on;  // ticks the note sounds for
    uint16_t off; // and ticks of silence after it
} note_t;

static uint16_t saved_screen[SCREEN_CELLS];
static uint16_t saved_cursor;
static uint8_t saved_colours[SHADES][3];
static uint8_t heat[HEIGHT][WIDTH];
static uint8_t fuel[WIDTH]; // how hard each column burns, which wanders and makes the tongues
static Random rng;

static note_t *notes;
static uint32_t note_count;

// written by the timer interrupt, read by the drawing loop
static volatile uint32_t frames_due;
static volatile uint32_t frame_countdown;
static volatile uint32_t note_at;
static volatile uint32_t phase_left;
static volatile bool_t sounding;
static volatile uint8_t flare; // full on the attack of a note, dying away after it

// Black, then red, then orange, then yellow, then white: six bits a channel
static const uint8_t fire_ramp[SHADES][3] = {
    {0, 0, 0},
    {10, 0, 0},
    {20, 0, 0},
    {30, 2, 0},
    {40, 6, 0},
    {48, 12, 0},
    {54, 18, 0},
    {58, 24, 0},
    {61, 30, 0},
    {63, 36, 0},
    {63, 42, 2},
    {63, 48, 6},
    {63, 54, 12},
    {63, 58, 22},
    {63, 61, 36},
    {63, 63, 55},
};

static uint32_t ticks_for(uint32_t ms)
{
    const uint32_t frequency = get_timer_frequency();

    return (ms / 1000) * frequency + (ms % 1000 * frequency + 999) / 1000;
}

static void tick(void)
{
    if (frame_countdown > 0 && --frame_countdown == 0)
    {
        frame_countdown = get_timer_frequency() / FRAMES_PER_SECOND;
        if (frames_due < 3)
            frames_due++;
    }

    flare = flare > 4 ? (uint8_t)(flare - 4) : 0;

    if (notes == NULL || phase_left-- > 0)
        return;

    if (sounding) // the note is over and its pause begins
    {
        speaker_off();
        sounding = false;
        phase_left = notes[note_at].off;
        note_at = note_at + 1 < note_count ? note_at + 1 : 0;
        return;
    }

    speaker_on(notes[note_at].hz);
    sounding = true;
    phase_left = notes[note_at].on;
    flare = 0xFF;
}

static void step_fire(void)
{
    for (uint32_t x = 0; x < WIDTH; x++)
    {
        const int32_t drift = (int32_t)random_next_bounded(&rng, 61) - 30;
        int32_t burning = (int32_t)fuel[x] + drift;

        if (burning < 0)
            burning = 0;
        if (burning > 255)
            burning = 255;

        fuel[x] = (uint8_t)burning;

        const uint32_t hot = (uint32_t)burning + (flare >> 2);
        heat[FUEL_ROW][x] = (uint8_t)(hot > 255 ? 255 : hot);
    }

    for (uint32_t y = 0; y < FUEL_ROW; y++)
    {
        for (uint32_t x = 0; x < WIDTH; x++)
        {
            const uint32_t left = heat[y + 1][x == 0 ? WIDTH - 1 : x - 1];
            const uint32_t middle = heat[y + 1][x];
            const uint32_t right = heat[y + 1][x + 1 == WIDTH ? 0 : x + 1];
            const uint32_t under = heat[y + 2 < HEIGHT ? y + 2 : FUEL_ROW][x];
            const uint32_t average = (left + middle + right + under) / 4;
            const uint32_t cooling = random_next_bounded(&rng, 11);

            heat[y][x] = (uint8_t)(average > cooling ? average - cooling : 0);
        }
    }
}

static void draw(void)
{
    for (uint32_t y = 0; y < SCREEN_ROWS; y++)
    {
        for (uint32_t x = 0; x < WIDTH; x++)
        {
            const uint16_t upper = (uint16_t)(heat[y * 2][x] >> 4);
            const uint16_t lower = (uint16_t)(heat[y * 2 + 1][x] >> 4);

            put_attrchar((uint16_t)(y * SCREEN_COLUMNS + x),
                         (uint16_t)((lower << 12) | (upper << 8) | HALF_BLOCK));
        }
    }
}

static uint32_t number_after(const char *line, char flag)
{
    for (uint32_t i = 0; line[i] != '\0'; i++)
    {
        if (line[i] != '-' || line[i + 1] != flag)
            continue;

        uint32_t value = 0;
        uint32_t at = i + 2;

        while (line[at] == ' ')
            at++;

        if (line[at] < '0' || line[at] > '9')
            return 0;

        while (line[at] >= '0' && line[at] <= '9')
            value = value * 10 + (uint32_t)(line[at++] - '0');

        return value;
    }

    return 0;
}

// the tune is a beep script on the volume, the same one the shell would play with run
static bool_t load_music(const char *path)
{
    fat32_entry_t entry;

    notes = NULL;
    note_count = 0;

    if (path == NULL || !fat32_stat(path, &entry) || entry.is_dir || entry.size == 0)
        return false;

    char *text = malloc(entry.size + 1);

    if (text == NULL)
        return false;

    uint32_t got = 0;
    while (got < entry.size)
    {
        const uint32_t piece = fat32_read_at(path, got, text + got, entry.size - got);

        if (piece == 0)
            break;

        got += piece;
    }

    text[got] = '\0';

    uint32_t lines = 1;
    for (uint32_t i = 0; i < got; i++)
        if (text[i] == '\n')
            lines++;

    notes = malloc(lines * sizeof(note_t));

    if (notes == NULL)
    {
        free(text);
        return false;
    }

    uint32_t at = 0;
    while (at < got && note_count < lines)
    {
        const char *line = text + at;

        while (at < got && text[at] != '\n')
            at++;
        if (at < got)
            text[at++] = '\0';

        const uint32_t hz = number_after(line, 'f');

        if (hz != 0)
        {
            notes[note_count].hz = (uint16_t)hz;
            notes[note_count].on = (uint16_t)ticks_for(number_after(line, 'l'));
            notes[note_count].off = (uint16_t)ticks_for(number_after(line, 'D'));
            note_count++;
        }
    }

    free(text);

    if (note_count == 0)
    {
        free(notes);
        notes = NULL;
        return false;
    }

    return true;
}

static void take_fire_palette(void)
{
    for (uint8_t i = 0; i < SHADES; i++)
    {
        vga_read_colour(i, saved_colours[i]);
        vga_write_colour(i, fire_ramp[i]);
    }

    vga_set_blink(false); // which frees the top bit of every attribute to be a background colour
}

static void give_palette_back(void)
{
    vga_set_blink(true);

    for (uint8_t i = 0; i < SHADES; i++)
        vga_write_colour(i, saved_colours[i]);
}

void screensaver_main(const char *music_path)
{
    for (uint16_t i = 0; i < SCREEN_CELLS; i++)
        saved_screen[i] = get_attrchar(i);

    saved_cursor = get_vga_cursor_pos();
    set_vga_cursor_visibility(false);

    random_init(&rng, (uint32_t)get_timer_ticks());

    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t x = 0; x < WIDTH; x++)
            heat[y][x] = 0;

    for (uint32_t x = 0; x < WIDTH; x++)
        fuel[x] = (uint8_t)random_next(&rng);

    frames_due = 0;
    frame_countdown = get_timer_frequency() / FRAMES_PER_SECOND;
    note_at = 0;
    phase_left = 0;
    sounding = false;
    flare = 0;

    load_music(music_path);
    take_fire_palette();
    register_pit_task(tick);

    while (get_keyboard_char() == 0)
    {
        if (frames_due == 0)
        {
            asm volatile("hlt");
            continue;
        }

        frames_due--;
        step_fire();
        draw();
    }

    pop_pit_task();
    speaker_off();

    if (notes != NULL)
    {
        free(notes);
        notes = NULL;
    }

    give_palette_back();

    for (uint16_t i = 0; i < SCREEN_CELLS; i++)
        put_attrchar(i, saved_screen[i]);

    set_vga_cursor_pos(saved_cursor);
    set_vga_cursor_visibility(true);
}
