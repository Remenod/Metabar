#include <kernel/memory.h>
#include <paging/paging.h>

#include <drivers/qemu_serial.h>

static block_t *heap_free_list = NULL;

// resets the heap. Call this before first malloc
void heap_init(void)
{
    for (uint32_t virt = HEAP_START; virt < HEAP_END; virt += 0x1000)
    {
        uint32_t ph = alloc_frame();
        map_page(virt, ph, PAGE_PRESENT | PAGE_RW);
        // serial_write_hex_uint32(virt);
        // serial_write_str(" -> ");
        // serial_write_hex_uint32(ph);
        // serial_write_char('\n');
    }

    heap_free_list = (block_t *)HEAP_START;
    heap_free_list->size = HEAP_END - HEAP_START - sizeof(block_t);
    heap_free_list->next = NULL;
}

void *malloc(uint32_t n)
{
    block_t *best = NULL;
    block_t *best_prev = NULL;

    for (block_t *curr = heap_free_list, *prev = NULL; curr != NULL; prev = curr, curr = curr->next)
    {
        if (curr->size >= n)
        {
            if (best == NULL || curr->size < best->size)
            {
                best = curr;
                best_prev = prev;
                if (curr->size == n)
                    break;
            }
        }
    }

    if (best == NULL)
        return NULL;

    if (best->size >= n + sizeof(block_t) + 4)
    {
        uint8_t *base = (uint8_t *)best;
        block_t *new_block = (block_t *)(base + sizeof(block_t) + n);
        new_block->size = best->size - n - sizeof(block_t);
        new_block->next = best->next;
        best->next = new_block;
        best->size = n;
    }

    if (best_prev == NULL)
        heap_free_list = best->next;
    else
        best_prev->next = best->next;

    serial_write_hex_uint32((uint8_t *)best + sizeof(block_t));
    serial_write_char('\n');

    return (uint8_t *)best + sizeof(block_t);
}

void free(void *ptr)
{
    if (!ptr)
        return;
    if ((uint32_t)ptr > HEAP_END || (uint32_t)ptr < HEAP_START)
        return;

    block_t *blk = (block_t *)((uint8_t *)ptr - sizeof(block_t));

    blk->next = heap_free_list;
    heap_free_list = blk;
}

void dump_heap(void)
{
    serial_write_char('\n');

    for (block_t *curr = heap_free_list, *prev = NULL; curr != NULL; prev = curr, curr = curr->next)
    {
        serial_write_hex_uint32(curr);
        serial_write_str(" -> SIZE: ");
        serial_write_hex_uint32(curr->size);
        serial_write_char('\n');
    }
}
