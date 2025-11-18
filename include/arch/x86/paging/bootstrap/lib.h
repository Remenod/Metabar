#pragma once

#include <lib/types.h>

void bootstrap_set_bitmap8_val(uint8_t bitmap[], size_t index, bool_t val);
bool_t bootstrap_get_bitmap8_val(uint8_t bitmap[], size_t index);
void *bootstrap_memcpy(void *dest, const void *src, uint32_t n);