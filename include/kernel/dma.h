#pragma once

#include <lib/types.h>

// virtual range the DMA allocator hands out from; sits between the heap and the temp window
#define DMA_WINDOW_START 0xE0000000
#define DMA_WINDOW_PAGES 4096 // 16 MiB

typedef struct
{
    void *virt;    // what the kernel uses
    uint32_t phys; // what the device is told
    uint32_t size; // rounded up to whole pages
} dma_buffer_t;

/* Buffer for memory a device reads or writes on its own: contiguous in PHYSICAL memory
 * (malloc only promises virtual contiguity), aligned both ways and zeroed.
 * `align` is rounded up to a power of two, at least one page.
 * Returns false if no run of frames or no room in the window is left.
 *
 * The pages stay cacheable: on x86 DMA is cache coherent, so only device REGISTERS
 * (MMIO) need caching turned off, not the buffers. */
bool_t dma_alloc(dma_buffer_t *buf, uint32_t size, uint32_t align);

// unmaps the buffer, returns its frames and clears the handle
void dma_free(dma_buffer_t *buf);
