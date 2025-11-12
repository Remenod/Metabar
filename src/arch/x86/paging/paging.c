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
extern void enable_global_pages(void);
extern void enable_paging(void);

static void setup_identity_mapping(void)
{
    for (uint32_t i = 0; i < 1024; i++)
    {
        set_bitmap8_val(avl_phys_pages_bitmap, i, true);
        pte_init(&page_table0[i], i * 0x1000, 1, 0, 0, 0, 0, 0, 0);
    }

    pde_init(&page_directories[0][0], (uint32_t)page_table0, 1, 0, 0, 0, 0, 0);

    for (uint32_t i = 1; i < 1024; i++)
    {
        page_directories[0][i].raw_data = 0;
        pde_set_present_flag(&page_directories[0][i], 0);
    }
}

void paging_init(void)
{
    asm volatile("cli");
    setup_identity_mapping();
    load_page_directory(page_directories[0]);
    enable_global_pages();
    enable_paging();
    asm volatile("sti");
}