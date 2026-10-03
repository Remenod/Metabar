#include <a20.h>

#include <ports.h>

// two addresses exactly 1 MiB apart: with A20 disabled they are the same memory
// 0x7000 is unused below the boot sector, nothing lives above 1 MiB yet
#define A20_TEST_LOW 0x7000
#define A20_TEST_HIGH (A20_TEST_LOW + 0x100000)

#define A20_TEST_SHORT 32
#define A20_TEST_LONG 0x10000 // the gate may take a while to switch after the 8042 command

#define KBC_DATA 0x60
#define KBC_STATUS 0x64
#define KBC_COMMAND 0x64
#define KBC_STATUS_OUTPUT_FULL 0x01
#define KBC_STATUS_INPUT_FULL 0x02
#define KBC_CMD_WRITE_OUTPUT_PORT 0xD1
#define KBC_OUTPUT_PORT_A20_ON 0xDF // A20 on, CPU reset line released
#define KBC_CMD_NULL 0xFF           // no-op, some USB legacy 8042 emulations need it after 0xD1
#define KBC_WAIT_TRIES 100000

#define SYSTEM_CONTROL_PORT_A 0x92
#define PORT_A_FAST_RESET 0x01
#define PORT_A_A20 0x02

#define IO_DELAY_PORT 0x80

static void io_delay(void)
{
    outb(IO_DELAY_PORT, 0);
}

static bool_t a20_test(uint32_t tries)
{
    volatile uint32_t *low = (volatile uint32_t *)A20_TEST_LOW;
    volatile uint32_t *high = (volatile uint32_t *)A20_TEST_HIGH;

    const uint32_t saved = *low;
    bool_t enabled = false;

    for (uint32_t i = 0; i < tries && !enabled; i++)
    {
        *low = i;
        *high = ~i; // lands on *low if the addresses alias
        enabled = *low == i;
        io_delay();
    }

    *low = saved;
    return enabled;
}

// waits until the 8042 can take a byte, dropping anything it has for us
// returns false if there is no controller or it never gets ready
static bool_t kbc_drain(void)
{
    for (uint32_t i = 0; i < KBC_WAIT_TRIES; i++)
    {
        const uint8_t status = inb(KBC_STATUS);
        if (status == 0xFF) // nothing on the bus
            return false;

        if (status & KBC_STATUS_OUTPUT_FULL)
            inb(KBC_DATA);
        else if (!(status & KBC_STATUS_INPUT_FULL))
            return true;

        io_delay();
    }
    return false;
}

static void a20_enable_kbc(void)
{
    if (!kbc_drain())
        return;
    outb(KBC_COMMAND, KBC_CMD_WRITE_OUTPUT_PORT);
    if (!kbc_drain())
        return;
    outb(KBC_DATA, KBC_OUTPUT_PORT_A20_ON);
    if (!kbc_drain())
        return;
    outb(KBC_COMMAND, KBC_CMD_NULL);
    kbc_drain();
}

static void a20_enable_fast(void)
{
    const uint8_t port_a = inb(SYSTEM_CONTROL_PORT_A);
    if (port_a & PORT_A_A20)
        return;
    outb(SYSTEM_CONTROL_PORT_A, (port_a | PORT_A_A20) & ~PORT_A_FAST_RESET); // bit 0 would reset the CPU
}

bool_t a20_enable(void)
{
    if (a20_test(A20_TEST_SHORT))
        return true;

    a20_enable_kbc();
    if (a20_test(A20_TEST_LONG))
        return true;

    a20_enable_fast();
    return a20_test(A20_TEST_LONG);
}
