#pragma once

#include <lib/types.h>

#define HEAP_START 0xD0000000
#define HEAP_END 0xE0000000
#define HEAP_GROW_MIN 0x10000 // heap is mapped on demand, at least this many bytes at a time
#define HEAP_MIN_SPLIT 8      // a leftover smaller than a header plus this stays inside the allocation

typedef struct block block_t;

typedef struct block
{
    uint32_t size;
    block_t *next;
} block_t;

void heap_init(void);

void *malloc(uint32_t n);

void *malloc_aligned(uint32_t n, uint32_t align);

void free(void *addr);
