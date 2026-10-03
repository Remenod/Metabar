#pragma once

#include <lib/types.h>

/* Enables the A20 line (8042 keyboard controller first, then the "fast A20" port 0x92) and checks
 * that memory 1 MiB apart is really distinct. Returns false if A20 stays disabled.
 * The check goes through the bootstrap identity mapping of the low 4 MiB,
 * so call it before the kernel page directory is loaded. */
bool_t a20_enable(void);
