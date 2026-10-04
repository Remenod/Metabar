#pragma once

#include <lib/types.h>

// where stage 2 describes the ramdisk it copied: magic, physical base, byte size
#define RAMDISK_INFO_ADDR 0x1600      // keep in sync with RAMDISK_INFO in stage2.asm
#define RAMDISK_MAGIC 0x4B444D52      // 'RMDK'
#define RAMDISK_WINDOW_START 0xC8000000 // virtual window it is mapped into
#define RAMDISK_WINDOW_SIZE 0x08000000  // 128 MiB, up to where the heap starts
#define RAMDISK_SECTOR_SIZE 512

/* Reads what stage 2 left in low memory. Must run before the kernel page directory replaces the
 * bootstrap identity mapping, and before the frame bitmap is built, which reserves these frames. */
void ramdisk_read_boot_info(void);

uint32_t ramdisk_phys_base(void);
uint32_t ramdisk_size(void);
uint32_t ramdisk_sectors(void);

// maps the ramdisk into its window; call after the kernel page directory is live
bool_t ramdisk_map(void);

// copies `count` sectors starting at `lba`; false if the range is outside the ramdisk
bool_t ramdisk_read(uint32_t lba, uint32_t count, void *buf);

// copies them the other way; the ramdisk is RAM, so what is written is lost at the next boot
bool_t ramdisk_write(uint32_t lba, uint32_t count, const void *buf);
