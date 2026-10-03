#include <kernel/ramdisk.h>

#include <paging/paging.h>
#include <lib/mem.h>

static uint32_t phys_base = 0;
static uint32_t byte_size = 0;
static bool_t mapped = false;

void ramdisk_read_boot_info(void)
{
    const volatile uint32_t *info = (const volatile uint32_t *)RAMDISK_INFO_ADDR;

    phys_base = 0;
    byte_size = 0;

    if (info[0] != RAMDISK_MAGIC)
        return;

    // whole pages only: the window is mapped page by page
    phys_base = info[1] & ~(PAGE_SIZE - 1);
    byte_size = info[2] & ~(PAGE_SIZE - 1);

    if (byte_size > RAMDISK_WINDOW_SIZE)
        byte_size = RAMDISK_WINDOW_SIZE;
}

uint32_t ramdisk_phys_base(void)
{
    return phys_base;
}

uint32_t ramdisk_size(void)
{
    return byte_size;
}

uint32_t ramdisk_sectors(void)
{
    return byte_size / RAMDISK_SECTOR_SIZE;
}

bool_t ramdisk_map(void)
{
    if (byte_size == 0)
        return false;

    for (uint32_t offset = 0; offset < byte_size; offset += PAGE_SIZE)
    {
        if (map_page(RAMDISK_WINDOW_START + offset, phys_base + offset, PAGE_PRESENT | PAGE_RW))
            continue;

        byte_size = offset; // keep whatever got mapped, the rest is unreachable
        break;
    }

    mapped = byte_size != 0;
    return mapped;
}

bool_t ramdisk_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!mapped || buf == NULL || count == 0)
        return false;

    if (lba > ramdisk_sectors() || count > ramdisk_sectors() - lba)
        return false;

    memcpy(buf, (const void *)(RAMDISK_WINDOW_START + lba * RAMDISK_SECTOR_SIZE),
           count * RAMDISK_SECTOR_SIZE);
    return true;
}
