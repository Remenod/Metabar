#pragma once

#include <lib/types.h>
#include <paging/page_directory.h>

extern uint8_t __phys_after_bootstrap_data; // from linker script
extern uint8_t __phys_after_kernel;         // from linker script

#define KERNEL_VMA 0xC0000000
#define KERNEL_PHYS_BASE (uint32_t)&__phys_after_bootstrap_data
#define KERNEL_PHYS_END (uint32_t)&__phys_after_kernel
#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_SIZE 0x1000
#define TOTAL_FRAMES (1024 * 1024)

// bootstrap maps this much physical memory, starting at KERNEL_PHYS_BASE, at KERNEL_VMA
#define KERNEL_WINDOW_SIZE 0x400000
#define HIGH_MEM_START 0x100000

// high-half kernel stack, defined in kernel_entry.asm (.bss); ESP is switched there before kernel_main
// the page right below it is unmapped, so an overflow faults instead of overwriting .bss
extern uint8_t kernel_stack_guard[];
extern uint8_t kernel_stack[];
extern uint8_t kernel_stack_top[];

// EBDA, VGA memory, option ROMs and BIOS ROM: never usable RAM
// E820 normally reports it as reserved too, this is a safety net for BIOSes that do not
#define LOW_MEM_RESERVED_START 0x9F000
#define LOW_MEM_RESERVED_END 0x100000

// BIOS memory map collected by boot.asm: uint32_t count, then entries
// readable only while the bootstrap identity mapping of low memory is active
#define E820_MAP_ADDR 0x1000 // keep in sync with E820_MAP in boot.asm
#define E820_MAX_ENTRIES 64  // keep in sync with E820_MAX_ENTRIES in boot.asm
#define E820_TYPE_USABLE 1

// 20-byte entries: boot.asm does not ask for ACPI 3.0 extended attributes
typedef struct __attribute__((packed))
{
    uint64_t base;
    uint64_t length;
    uint32_t type;
} e820_entry_t;

#define TEMP_PD_VADDR 0xF0000000

// returns false if there is not enough usable RAM for the frame bitmap and the kernel page directory (e.g. no E820 map)
// needs A20 enabled: the frame bitmap is placed above 1 MiB
bool_t setup_high_half_selfcontained_paging(void);

static inline void *phys_to_vir_addr(uint32_t phys)
{
    return (void *)((phys - KERNEL_PHYS_BASE) + KERNEL_VMA);
}

static inline void *vir_to_phys_addr(void *virt)
{
    return (void *)(((uint32_t)virt - KERNEL_VMA) + KERNEL_PHYS_BASE);
}

bool_t map_page(uint32_t virt, uint32_t phys, uint32_t flags);

void unmap_page(uint32_t virt);

uint32_t alloc_frame(void);

// `pages` frames in a row, first one aligned to `align` bytes; 0 if there is no such run
uint32_t alloc_contiguous_frames(uint32_t pages, uint32_t align);

void free_frame(uint32_t phys_addr);

void free_frames(uint32_t phys_addr, uint32_t pages);

uint32_t virt_to_phys(uint32_t virt);

volatile pde_t *create_page_directory(void);
