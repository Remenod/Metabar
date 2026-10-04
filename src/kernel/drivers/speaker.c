#include <drivers/speaker.h>

#include <ports.h>
#include <timer/pit.h>

#define PIT_HZ 1193180
#define PIT_CHANNEL_2 0x42
#define PIT_COMMAND 0x43
#define PIT_SQUARE_WAVE 0xB6 // channel 2, both bytes of the divisor, mode 3, counting in binary

#define SPEAKER_PORT 0x61
#define SPEAKER_BITS 0x03 // one bit opens the gate of the timer, the other connects the cone

static bool_t sounding = false;

/* Channel 2 of the same timer that keeps the clock is wired to the speaker instead of to an
 * interrupt, so setting a tone means dividing its 1.19 MHz down to the frequency wanted. */
void speaker_on(uint32_t hz)
{
    if (hz < SPEAKER_MIN_HZ || hz > SPEAKER_MAX_HZ)
        return;

    const uint16_t divisor = (uint16_t)(PIT_HZ / hz);

    outb(PIT_COMMAND, PIT_SQUARE_WAVE);
    outb(PIT_CHANNEL_2, (uint8_t)divisor);
    outb(PIT_CHANNEL_2, (uint8_t)(divisor >> 8));

    outb(SPEAKER_PORT, (uint8_t)(inb(SPEAKER_PORT) | SPEAKER_BITS));
    sounding = true;
}

void speaker_off(void)
{
    outb(SPEAKER_PORT, (uint8_t)(inb(SPEAKER_PORT) & ~SPEAKER_BITS));
    sounding = false;
}

bool_t speaker_is_on(void)
{
    return sounding;
}

void beep(uint32_t hz, uint32_t milliseconds)
{
    speaker_on(hz);
    sleep(milliseconds);
    speaker_off();
}
