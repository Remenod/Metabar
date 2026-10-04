#pragma once

#include <lib/types.h>

/* The PC speaker can do one thing: a square wave at one frequency, for as long as it is left on.
 * The timer makes the wave, so the range is what a sixteen bit divisor of 1.19 MHz can reach. */
#define SPEAKER_MIN_HZ 19
#define SPEAKER_MAX_HZ 20000

void speaker_on(uint32_t hz);
void speaker_off(void);
bool_t speaker_is_on(void);

// one tone that lasts as long as it is told to, which is time the caller spends waiting
void beep(uint32_t hz, uint32_t milliseconds);
