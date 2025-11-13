#include <paging/paging.h>
#include <paging/page_table.h>
#include <paging/page_directory.h>
#include <lib/arrlib.h>

#define PAGE_SIZE 1024 * 4
#define TOTAL_FRAMES 1024 * 1024

static uint8_t avl_phys_pages_bitmap[TOTAL_FRAMES / 8] = {0};

static pde_t page_directories[64][1024] __attribute__((aligned(4096)));
static pte_t page_table0[1024] __attribute__((aligned(4096)));

extern void load_page_directory(pde_t page_dir[1024]);

uint32_t *get_page_directory_vir_addr(void)
{
    return page_directories[0];
}

uint32_t alloc_frame(void)
{
    for (uint32_t i = 0; i < TOTAL_FRAMES; ++i)
    {
        if (!get_bitmap8_val(avl_phys_pages_bitmap, i))
        {
            set_bitmap8_val(avl_phys_pages_bitmap, i, true);
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

void free_frame(uint32_t phys_addr)
{
    uint32_t frame = phys_addr / PAGE_SIZE;
    set_bitmap8_val(avl_phys_pages_bitmap, true, false);
}

uint32_t *alloc_page_table()
{
    uint32_t *pt = alloc_frame();
    memset(pt, 0, 4096);
    return pt;
}
