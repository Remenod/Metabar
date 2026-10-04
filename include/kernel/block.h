#pragma once

#include <lib/types.h>

#define BLOCK_SECTOR_SIZE 512
#define BLOCK_MAX_DEVICES 4

typedef struct block_device
{
    const char *name;
    uint32_t sectors;
    bool_t (*read)(uint32_t lba, uint32_t count, void *buf);
    bool_t (*write)(uint32_t lba, uint32_t count, const void *buf); // NULL when the device is read only
} block_device_t;

// every driver that can hold a filesystem puts itself on the list at boot
void block_register(const block_device_t *device);

uint32_t block_count(void);
const block_device_t *block_at(uint32_t index);
const block_device_t *block_find(const char *name);

// the device reads and writes go to; NULL until a filesystem picks one
const block_device_t *block_device(void);
void block_select(const block_device_t *device);

// read and write through the selected device; false if there is none or the range is outside it
bool_t block_read(uint32_t lba, uint32_t count, void *buf);
bool_t block_write(uint32_t lba, uint32_t count, const void *buf);
