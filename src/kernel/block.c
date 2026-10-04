#include <kernel/block.h>

static const block_device_t *current = NULL;

static bool_t range_ok(uint32_t lba, uint32_t count)
{
    return count != 0 && lba < current->sectors && count <= current->sectors - lba;
}

const block_device_t *block_device(void)
{
    return current;
}

void block_register(const block_device_t *device)
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
