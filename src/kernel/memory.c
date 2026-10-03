#include <kernel/memory.h>
#include <paging/paging.h>

#include <drivers/qemu_serial.h>

static block_t *heap_free_list = NULL;

// end of the mapped part of the heap: [HEAP_START, heap_top) is backed by frames
static uint32_t heap_top = HEAP_START;

/* Maps at least `bytes` more heap pages (one frame at a time, no physical contiguity needed)
 * and adds them to the free list. Returns false if fewer than `bytes` could be mapped. */
static bool_t heap_grow(uint32_t bytes)
{
    uint32_t need = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t left = HEAP_END - heap_top;
    if (need == 0 || need > left)
        return false;

    uint32_t grow = need < HEAP_GROW_MIN ? HEAP_GROW_MIN : need;
    if (grow > left)
        grow = left;

    uint32_t region = heap_top;
    uint32_t mapped = 0;
    while (mapped < grow)
    {
        uint32_t phys = alloc_frame();
        if (!phys)
            break;
        if (!map_page(heap_top, phys, PAGE_PRESENT | PAGE_RW))
        {
            free_frame(phys);
            break;
        }
        heap_top += PAGE_SIZE;
        mapped += PAGE_SIZE;
    }

    if (mapped == 0)
        return false;

    // hand the new region to free(), which also merges it with a free block right before it
    block_t *blk = (block_t *)region;
    blk->size = mapped - sizeof(block_t);
    free((uint8_t *)blk + sizeof(block_t));

    return mapped >= need;
}

// resets the heap. Call this before first malloc
void heap_init(void)
{
    heap_free_list = NULL;
    heap_top = HEAP_START;
    heap_grow(HEAP_GROW_MIN);
}

/* Payload address this block would hand out for the requested alignment.
 * A gap in front of it becomes a free block of its own, so it must be either empty
 * or big enough to hold a header plus a usable tail. */
static uint32_t aligned_payload(const block_t *blk, uint32_t align)
{
    const uint32_t natural = (uint32_t)blk + sizeof(block_t);
    uint32_t payload = (natural + align - 1) & ~(align - 1);

    while (payload != natural && payload - natural < sizeof(block_t) + HEAP_MIN_SPLIT)
        payload += align;

    return payload;
}

// smallest free block that can serve `n` bytes at `align`; *payload gets the address inside it
static block_t *find_fit(uint32_t n, uint32_t align, block_t **best_prev, uint32_t *payload)
{
    block_t *best = NULL;
    *best_prev = NULL;

    for (block_t *curr = heap_free_list, *prev = NULL; curr != NULL; prev = curr, curr = curr->next)
    {
        const uint32_t end = (uint32_t)curr + sizeof(block_t) + curr->size;
        const uint32_t start = aligned_payload(curr, align);

        if (start > end || end - start < n)
            continue;

        if (best == NULL || curr->size < best->size)
        {
            best = curr;
            *best_prev = prev;
            *payload = start;
        }
    }

    return best;
}

/* Allocates `n` bytes whose address is a multiple of `align` (rounded up to a power of two,
 * at least 8). Needed for hardware structures: xHCI rings want 64 bytes, buffers want a page.
 * The memory is virtually contiguous but its frames are scattered — use dma_alloc for devices. */
void *malloc_aligned(uint32_t n, uint32_t align)
{
    if (n == 0 || n > HEAP_END - HEAP_START)
        return NULL;

    if (align < 8)
        align = 8;
    while (align & (align - 1)) // round up to a power of two
        align += align & (~align + 1);

    n = (n + 7) & ~7;

    block_t *best_prev;
    uint32_t payload;
    block_t *best = find_fit(n, align, &best_prev, &payload);

    if (best == NULL)
    {
        heap_grow(n + align + 2 * sizeof(block_t));
        best = find_fit(n, align, &best_prev, &payload);
        if (best == NULL)
            return NULL;
    }

    const uint32_t block_end = (uint32_t)best + sizeof(block_t) + best->size;
    const uint32_t head_gap = payload - ((uint32_t)best + sizeof(block_t));
    block_t *allocated = (block_t *)(payload - sizeof(block_t));
    block_t *next_free = best->next;

    if (head_gap == 0) // the block itself becomes the allocation, drop it from the list
    {
        if (best_prev == NULL)
            heap_free_list = best->next;
        else
            best_prev->next = best->next;
    }
    else // the part in front stays free
    {
        best->size = head_gap - sizeof(block_t);
    }

    const uint32_t tail = block_end - (payload + n);
    if (tail >= sizeof(block_t) + HEAP_MIN_SPLIT)
    {
        block_t *rest = (block_t *)(payload + n);
        rest->size = tail - sizeof(block_t);
        rest->next = next_free;

        if (head_gap != 0)
            best->next = rest;
        else if (best_prev == NULL)
            heap_free_list = rest;
        else
            best_prev->next = rest;

        allocated->size = n;
    }
    else // too small to split off, the allocation keeps it
    {
        allocated->size = block_end - payload;
    }

    return (void *)payload;
}

void *malloc(uint32_t n)
{
    return malloc_aligned(n, 8);
}

void free(void *ptr)
{
    if (!ptr)
        return;

    if ((uint32_t)ptr < HEAP_START + sizeof(block_t) || (uint32_t)ptr >= heap_top)
        return;

    block_t *blk = (block_t *)((uint8_t *)ptr - sizeof(block_t));

    block_t *curr = heap_free_list;
    block_t *prev = NULL;

    while (curr && curr < blk)
    {
        prev = curr;
        curr = curr->next;
    }

    blk->next = curr;

    if (prev)
        prev->next = blk;
    else
        heap_free_list = blk;

    if (blk->next && (uint8_t *)blk + sizeof(block_t) + blk->size == (uint8_t *)blk->next)
    {
        blk->size += sizeof(block_t) + blk->next->size;
        blk->next = blk->next->next;
    }

    if (prev && (uint8_t *)prev + sizeof(block_t) + prev->size == (uint8_t *)blk)
    {
        prev->size += sizeof(block_t) + blk->size;
        prev->next = blk->next;
    }
}

void dump_heap(void)
{
    serial_write_char('\n');

    for (block_t *curr = heap_free_list; curr != NULL; curr = curr->next)
    {
        serial_write_hex_uint32((uint32_t)curr);
        serial_write_str(" -> SIZE: ");
        serial_write_hex_uint32(curr->size);
        serial_write_char('\n');
    }
}
