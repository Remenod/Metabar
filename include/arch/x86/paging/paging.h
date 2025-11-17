#pragma once

#include <lib/types.h>

extern uint8_t __phys_after_bootstrap_data; // from linker script
extern uint8_t __phys_after_kernel;         // from linker script

#define KERNEL_OFFSET 0x9000
#define KERNEL_VMA 0xC0000000
#define KERNEL_PHYS_BASE (uint32_t)&__phys_after_bootstrap_data
#define KERNEL_PHYS_END (uint32_t)&__phys_after_kernel
#define PAGE_SIZE 1024 * 4
#define TOTAL_FRAMES 1024 * 1024

#define HIGH_HALF_STACK_CAPACITY 0x3FFFC

#define HIGH_HALF_STACK_BASE (KERNEL_VMA + (KERNEL_PHYS_END - KERNEL_PHYS_BASE))
#define HIGH_HALF_STACK_TOP (HIGH_HALF_STACK_BASE + HIGH_HALF_STACK_CAPACITY)

#define BOOTSTRAP_STACK_BASE 0x60000
#define BOOTSTRAP_STACK_TOP 0x9FFFC

void init_kernel_page_directory(void);

inline void *phys_to_vir_addr(uint32_t phys)
{
    return (void *)((phys - KERNEL_PHYS_BASE) + KERNEL_VMA);
}

inline uint32_t vir_to_phys_addr(void *virt)
{
    return ((uint32_t)virt - KERNEL_VMA) + KERNEL_PHYS_BASE;
}
