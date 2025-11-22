#pragma once

#include <lib/types.h>

#define HEAP_START 0xD0000000
#define HEAP_END 0xD1000000

typedef struct block block_t;

typedef struct block
{
    uint32_t size;
    block_t *next;
} block_t;

void heap_init(void);

void *malloc(uint32_t n);

void free(void *addr);
