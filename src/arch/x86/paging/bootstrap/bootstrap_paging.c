#include <lib/types.h>
#include <paging/page_table.h>
#include <paging/page_directory.h>
#include <paging/bootstrap/lib.h>
#include <paging/paging.h> // only 4 get_page_directories_vir_addr after mapping kernel n enabling paging

__attribute__((section(".bootstrap"))) extern void bootstrap_load_page_directory(pde_t page_dir[1024]);
__attribute__((section(".bootstrap"))) extern void bootstrap_enable_global_pages(void);
__attribute__((section(".bootstrap"))) extern void bootstrap_enable_paging(void);

__attribute__((section(".bootstrap_rodata"), aligned(4096))) pde_t bootstrap_page_directory[1024] = {0};
__attribute__((section(".bootstrap_rodata"), aligned(4096))) pte_t bootstrap_page_table[1024] = {0};
__attribute__((section(".bootstrap_rodata"), aligned(4096))) pte_t bootstrap_page_table_kernel[1024] = {0};

extern uint8_t __phys_after_bootstrap_data; // from linker script

#define KERNEL_VMA 0xC0000000
#define KERNEL_PHYS_BASE (uint32_t)&(__phys_after_bootstrap_data)
#define PAGE_LEN 0x1000
#define PDE_COUNT 1024
#define AVL_PHYS_PAGES_BITMAP_SIZE 1024 * 1024 / 8

__attribute__((section(".bootstrap"))) void bootstrap_setup_mapping(void)
{
    const uint32_t kernel_phys_page_start = KERNEL_PHYS_BASE / PAGE_LEN;
    const uint32_t dir_index_kernel = KERNEL_VMA / (PAGE_LEN * 1024);
    for (uint32_t i = 0; i < 1024; i++)
    {
        // identity mapping for low 4MB (bootstrap)
        pte_init(&bootstrap_page_table[i],
                 (uint32_t)(i * PAGE_LEN), 1, 0, 0, 0, 0, 0, 0);

        // kernel mapping: virtual (KERNEL_VMA + i*PAGE_LEN) -> physical (KERNEL_PHYS_BASE + i*PAGE_LEN)
        pte_init(&bootstrap_page_table_kernel[i],
                 (KERNEL_PHYS_BASE + i * PAGE_LEN), 1, 0, 0, 0, 0, 1, 0);

        // note: simplify: (kernel_phys_page_start + i) * PAGE_LEN
    }

    for (uint32_t i = 1; i < 1024; i++)
    {
        pde_init(&bootstrap_page_directory[i], (uint32_t)bootstrap_page_table, 0, 0, 0, 0, 0, 0);
        pde_set_present_flag(&bootstrap_page_directory[i], 0);
    }

    // make sure you pass *physical* address of page tables into PDEs.
    // If bootstrap_page_table is identity-mapped and its address equals its physical address, this is OK:
    pde_init(&bootstrap_page_directory[0], (uint32_t)bootstrap_page_table, 1, 0, 0, 0, 0, 0);
    pde_init(&bootstrap_page_directory[dir_index_kernel], bootstrap_page_table_kernel, 1, 0, 0, 0, 0, 0);
}

__attribute__((section(".bootstrap"))) void bootstrap_setup_page_directory(void)
{
    bootstrap_load_page_directory(bootstrap_page_directory);
}
