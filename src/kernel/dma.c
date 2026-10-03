#include <kernel/dma.h>

#include <paging/paging.h>
#include <lib/arrlib.h>
#include <lib/mem.h>

// 1 = page of the window is taken
static uint8_t window_pages[DMA_WINDOW_PAGES / 8] = {0};

// first run of `pages` free window pages whose address is a multiple of `align`
static bool_t find_window_run(uint32_t pages, uint32_t align, uint32_t *run_start)
{
    const uint32_t step = align > PAGE_SIZE ? align / PAGE_SIZE : 1;

    for (uint32_t start = 0; start + pages <= DMA_WINDOW_PAGES; start += step)
    {
        uint32_t i = start;
        while (i < start + pages && !get_bitmap8_val(window_pages, i))
            ++i;

        if (i == start + pages)
        {
            *run_start = start;
            return true;
        }
    }

    return false;
}

bool_t dma_alloc(dma_buffer_t *buf, uint32_t size, uint32_t align)
{
    if (buf == NULL || size == 0)
        return false;

    if (align < PAGE_SIZE) // the window is handed out page by page anyway
        align = PAGE_SIZE;
    while (align & (align - 1)) // round up to a power of two
        align += align & (~align + 1);

    const uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    uint32_t start;
    if (!find_window_run(pages, align, &start))
        return false;

    const uint32_t phys = alloc_contiguous_frames(pages, align);
    if (!phys)
        return false;

    const uint32_t virt = DMA_WINDOW_START + start * PAGE_SIZE;
    for (uint32_t i = 0; i < pages; ++i)
    {
        if (map_page(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, PAGE_PRESENT | PAGE_RW))
            continue;

        while (i-- > 0)
            unmap_page(virt + i * PAGE_SIZE);
        free_frames(phys, pages);
        return false;
    }

    for (uint32_t i = 0; i < pages; ++i)
        set_bitmap8_val(window_pages, start + i, true);

    memset((void *)virt, 0, pages * PAGE_SIZE);

    buf->virt = (void *)virt;
    buf->phys = phys;
    buf->size = pages * PAGE_SIZE;
    return true;
}

void dma_free(dma_buffer_t *buf)
{
    if (buf == NULL || buf->virt == NULL)
        return;

    const uint32_t virt = (uint32_t)buf->virt;
    const uint32_t pages = buf->size / PAGE_SIZE;
    const uint32_t start = (virt - DMA_WINDOW_START) / PAGE_SIZE;

    for (uint32_t i = 0; i < pages; ++i)
    {
        unmap_page(virt + i * PAGE_SIZE);
        set_bitmap8_val(window_pages, start + i, false);
    }

    free_frames(buf->phys, pages);

    buf->virt = NULL;
    buf->phys = 0;
    buf->size = 0;
}
