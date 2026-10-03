#pragma once

#include <lib/types.h>

#define BLOCK_SECTOR_SIZE 512

typedef struct block_device
{
    const char *name;
    uint32_t sectors;
    bool_t (*read)(uint32_t lba, uint32_t count, void *buf);
} block_device_t;

// the device filesystems are read from; NULL until a driver registers one
const block_device_t *block_device(void);

void block_register(const block_device_t *device);

// reads through the registered device; false if there is none or the range is outside it
bool_t block_read(uint32_t lba, uint32_t count, void *buf);
