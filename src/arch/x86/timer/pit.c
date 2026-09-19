#include <timer/pit.h>

#include <ports.h>
#include <interrupts/isr.h>
#include <interrupts/pic.h>

static volatile uint64_t timer_ticks = 0;

static volatile uint32_t timer_frequency;

static func_t pit_tasks[255];

static uint8_t task_count = 0;

void pit_handler(const cpu_state_t *state)
{
    (void)state;
    timer_ticks++;
    for (uint8_t i = 0; i < task_count; i++)
        pit_tasks[i]();
}

void register_pit_task(func_t task)
{
    if (task_count < (sizeof(pit_tasks) / sizeof(pit_tasks[0])))
        pit_tasks[task_count++] = task;
}

void pop_pit_task(void)
{
    if (task_count > 0)
        task_count--;
}

void pit_init(uint32_t frequency)
{
    timer_frequency = frequency;
    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / frequency);

    // Programming PIT: mode 2 (rate generator), access to both bytes, channel 0
    outb(PIT_COMMAND_PORT, 0x36);

    // Send the least significant byte of the divisor
    outb(PIT_CHANNEL0_PORT, divisor & 0xFF);

    // Send the most significant byte of the divisor
    outb(PIT_CHANNEL0_PORT, (divisor >> 8) & 0xFF);

    register_interrupt_handler(IRQ_TIMER + 32, pit_handler);
    pic_unmask_irq(IRQ_TIMER);
}

uint64_t get_timer_ticks(void)
{
    return timer_ticks;
}

uint32_t get_timer_frequency(void)
{
    return timer_frequency;
}

void sleep(uint32_t ms)
{
    uint64_t target = get_timer_ticks() + ms;
    while (get_timer_ticks() < target)
        asm volatile("hlt");
}
