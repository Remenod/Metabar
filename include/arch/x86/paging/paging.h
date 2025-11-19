#pragma once

#include <lib/types.h>
#include <paging/page_directory.h>

extern uint8_t __phys_after_bootstrap_data; // from linker script
extern uint8_t __phys_after_kernel;         // from linker script

#define KERNEL_OFFSET 0x9000
#define KERNEL_VMA 0xC0000000
#define KERNEL_PHYS_BASE (uint32_t)&__phys_after_bootstrap_data
#define KERNEL_PHYS_END (uint32_t)&__phys_after_kernel
#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_SIZE 1024 * 4
#define TOTAL_FRAMES 1024 * 1024

#define HIGH_HALF_STACK_CAPACITY 0x3FFFC

#define HIGH_HALF_STACK_BASE (KERNEL_VMA + (KERNEL_PHYS_END - KERNEL_PHYS_BASE))
#define HIGH_HALF_STACK_TOP (HIGH_HALF_STACK_BASE + HIGH_HALF_STACK_CAPACITY)

#define BOOTSTRAP_STACK_BASE 0x60000
#define BOOTSTRAP_STACK_TOP 0x9FFFC

#define TEMP_PD_VADDR 0xF0000000

void setup_high_half_selfcontained_paging(void);

inline void *phys_to_vir_addr(uint32_t phys)
{
    return (void *)((phys - KERNEL_PHYS_BASE) + KERNEL_VMA);
}

inline uint32_t vir_to_phys_addr(void *virt)
{
    return ((uint32_t)virt - KERNEL_VMA) + KERNEL_PHYS_BASE;
}

void map_page(uint32_t virt, uint32_t phys, uint32_t flags);

void unmap_page(uint32_t virt);

uint32_t alloc_frame(void);

void free_frame(uint32_t phys_addr);

volatile pde_t *create_page_directory(void);
