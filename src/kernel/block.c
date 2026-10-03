#include <kernel/block.h>

static const block_device_t *current = NULL;

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
    if (current == NULL || buf == NULL || count == 0)
        return false;

    if (lba > current->sectors || count > current->sectors - lba)
        return false;

    return current->read(lba, count, buf);
}
