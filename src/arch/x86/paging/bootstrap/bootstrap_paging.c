#include <lib/types.h>
#include <paging/page_table.h>
#include <paging/page_directory.h>
#include <paging/bootstrap/lib.h>

#include <paging/bootstrap/bootstrap_qemu_serial.h>

__attribute__((section(".bootstrap"))) extern void bootstrap_load_page_directory(pde_t page_dir[1024]);
__attribute__((section(".bootstrap"))) extern void bootstrap_enable_global_pages(void);
__attribute__((section(".bootstrap"))) extern void bootstrap_enable_paging(void);

__attribute__((section(".bootstrap.data"), aligned(4096))) pde_t bootstrap_page_directory[1024] = {0};
__attribute__((section(".bootstrap.data"), aligned(4096))) pte_t bootstrap_page_table[1024] = {0};
__attribute__((section(".bootstrap.data"), aligned(4096))) pte_t bootstrap_page_table_kernel[1024] = {0};
__attribute__((section(".bootstrap.data"), aligned(4096))) pte_t bootstrap_page_table_vga_vram[1024] = {0};

extern uint8_t __phys_after_bootstrap_data; // from linker script

#define KERNEL_VMA 0xC0000000
#define KERNEL_PHYS_BASE (uint32_t)&(__phys_after_bootstrap_data)
#define PAGE_LEN 0x1000
#define PDE_COUNT 1024
#define AVL_PHYS_PAGES_BITMAP_SIZE 1024 * 1024 / 8
#define VGA_PHYS_START 0xA0000
#define VGA_PHYS_END 0xBFFFF
#define VGA_VIRT_START 0xC10A0000

__attribute__((section(".bootstrap"))) void bootstrap_dump_mappings(pde_t *page_directory)
{
    for (uint32_t pdi = 0; pdi < 1024; pdi++)
    {
        pde_t pde = page_directory[pdi];
        if (!pde.fields.present)
            continue;
        if (pde.fields.ps)
            continue;

        pte_t *page_table = (pte_t *)(pde.fields.addr << 12);

        for (uint32_t pti = 0; pti < 1024; pti++)
        {
            pte_t pte = page_table[pti];
            if (!pte.fields.present)
                continue;

            uint32_t virt = (pdi << 22) | (pti << 12);
            uint32_t phys = pte.fields.addr << 12;

            bootstrap_serial_write_hex_uint32(phys);
            bootstrap_serial_write_char('-');
            bootstrap_serial_write_char('>');
            bootstrap_serial_write_hex_uint32(virt);
            bootstrap_serial_write_char('\n');
        }
    }
}

__attribute__((section(".bootstrap"))) static void bootstrap_remap_vga_vram(void)
{
    const uint32_t pde_index = VGA_VIRT_START >> 22;

    pde_init(&bootstrap_page_directory[pde_index], (uint32_t)bootstrap_page_table_vga_vram, 1, 0, 0, 0, 0, 0);

    uint32_t phys = VGA_PHYS_START;
    uint32_t virt = VGA_VIRT_START;

    for (int i = 0; phys <= VGA_PHYS_END; i++, phys += 0x1000, virt += 0x1000)
    {
        pte_init(&bootstrap_page_table_vga_vram[i], phys, 1, 0, 0, 1, 0, 1, 0);
    }
}

__attribute__((section(".bootstrap"))) void bootstrap_setup_mapping(void)
{
    const uint32_t dir_index_kernel = KERNEL_VMA >> 22;
    const uint32_t dir_index_vga = VGA_VIRT_START >> 22;

    for (uint32_t i = 0; i < 1024; i++)
    {
        pte_init(&bootstrap_page_table[i], i * PAGE_LEN, 1, 0, 0, 0, 0, 0, 0);

        pte_init(&bootstrap_page_table_kernel[i], KERNEL_PHYS_BASE + i * PAGE_LEN, 1, 0, 0, 0, 0, 0, 0);

        pte_init(&bootstrap_page_table_vga_vram[i], VGA_PHYS_START + i * PAGE_LEN, 1, 0, 0, 0, 0, 0, 0);
    }

    pde_init(&bootstrap_page_directory[0], (uint32_t)bootstrap_page_table, 1, 1, 0, 0, 0, 0);
    pde_init(&bootstrap_page_directory[dir_index_kernel], (uint32_t)bootstrap_page_table_kernel, 1, 0, 0, 0, 0, 0);
    pde_init(&bootstrap_page_directory[dir_index_vga], (uint32_t)bootstrap_page_table_vga_vram, 1, 0, 0, 0, 0, 0);

    pde_init(&bootstrap_page_directory[1023], (uint32_t)bootstrap_page_directory, 1, 1, 0, 0, 0, 0);

    bootstrap_remap_vga_vram();
    bootstrap_load_page_directory(bootstrap_page_directory);
}
