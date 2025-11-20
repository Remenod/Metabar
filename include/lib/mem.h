#pragma once

#include <lib/types.h>

void *memcpy(void *dest, const void *src, uint32_t n);

void memmove(void *dst, const void *src, size_t size);

void *memset(void *dst, int value, unsigned count);

void pokeb(unsigned seg, unsigned off, uint8_t val);

uint8_t peekb(unsigned seg, unsigned off);

void pokew(unsigned seg, unsigned off, uint16_t val);

uint16_t peekw(void *addr);
