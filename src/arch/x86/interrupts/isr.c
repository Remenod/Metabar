#include <interrupts/isr.h>

#include <ports.h>

static isr_t interrupt_handlers[IDT_ENTRIES];

void isr_common_handler(uint32_t int_no)
{
    if (interrupt_handlers[int_no])
        interrupt_handlers[int_no](NULL);

    if (int_no >= 40 && int_no < 48)
        outb(PIC2_COMMAND, PIC_EOI); // slave

    if (int_no >= 32)
        outb(PIC1_COMMAND, PIC_EOI); // master
}

void isr_exception_handler(uint32_t int_no, cpu_state_t state)
{
    if (interrupt_handlers[int_no])
        interrupt_handlers[int_no](&state);
}
void isr_stateless_exception_handler(
    uint32_t int_no,
    uint32_t eip,
    uint32_t cs,
    uint32_t eflags)
{
    cpu_state_t state = {0};
    state.eip = eip;
    state.cs = cs;
    state.eflags = eflags;

    if (interrupt_handlers[int_no])
        interrupt_handlers[int_no](&state);
}

void register_interrupt_handler(uint32_t int_no, isr_t handler)
{
    interrupt_handlers[int_no] = handler;
}
