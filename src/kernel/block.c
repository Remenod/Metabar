#include <kernel/block.h>

#include <lib/string.h>

static const block_device_t *devices[BLOCK_MAX_DEVICES];
static uint32_t devices_count = 0;
static const block_device_t *current = NULL;

static bool_t range_ok(uint32_t lba, uint32_t count)
{
    return count != 0 && lba < current->sectors && count <= current->sectors - lba;
}

void block_register(const block_device_t *device)
{
    if (device != NULL && devices_count < BLOCK_MAX_DEVICES)
        devices[devices_count++] = device;
}

uint32_t block_count(void)
{
    return devices_count;
}

const block_device_t *block_at(uint32_t index)
{
    return index < devices_count ? devices[index] : NULL;
}

const block_device_t *block_find(const char *name)
{
    for (uint32_t i = 0; i < devices_count; i++)
        if (strcmp(devices[i]->name, name) == 0)
            return devices[i];

    return NULL;
}

const block_device_t *block_device(void)
{
    return current;
}

void block_select(const block_device_t *device)
{
    current = device;
}

bool_t block_read(uint32_t lba, uint32_t count, void *buf)
{
    if (current == NULL || buf == NULL || !range_ok(lba, count))
        return false;

    return current->read(lba, count, buf);
}

bool_t block_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (current == NULL || current->write == NULL || buf == NULL || !range_ok(lba, count))
        return false;

    return current->write(lba, count, buf);
}
