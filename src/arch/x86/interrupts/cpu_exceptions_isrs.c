#include <interrupts/isr.h>
#include <kernel/diagnostics/rsod_routine.h>

#define DEFINE_UNSPECIAL_ISR(n, msg)                 \
    _Noreturn void isr_##n(const cpu_state_t *state) \
    {                                                \
        show_rsod(msg, state, n);                    \
        __builtin_unreachable();                     \
    }

// add implemented isr vec here
#define ISR_TO_REGISTRATION X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) \
    X(11) X(12) X(13) X(14) X(16) X(17) X(18) X(19) X(48)

DEFINE_UNSPECIAL_ISR(0, "Division by Zero");
DEFINE_UNSPECIAL_ISR(2, "NMI - Hardware Critical")
DEFINE_UNSPECIAL_ISR(4, "Signed Int Overflow");
DEFINE_UNSPECIAL_ISR(5, "Bound Range Exceeded");
DEFINE_UNSPECIAL_ISR(6, "Invalid Opcode");
DEFINE_UNSPECIAL_ISR(8, "Double Fault");
DEFINE_UNSPECIAL_ISR(9, "Coprocessor Segment Overrun");
/* 10 Invalid TSS */ // TODO implement if TSS will be used
DEFINE_UNSPECIAL_ISR(11, "Segment Not Present");
DEFINE_UNSPECIAL_ISR(12, "Stack Segment Fault"); // TODO reimplement after Rings system will be implemented(also special common isr needed)
DEFINE_UNSPECIAL_ISR(13, "General Protection Fault");
DEFINE_UNSPECIAL_ISR(14, "Page Fault");
DEFINE_UNSPECIAL_ISR(16, "FPU Floating-Point Error");
DEFINE_UNSPECIAL_ISR(17, "Unaligned data operation");
DEFINE_UNSPECIAL_ISR(18, "Machine Check - CPU Hardware Failure");
DEFINE_UNSPECIAL_ISR(19, "SIMD Floating-Point Error");
/* 20 Virtualization Exception */ // TODO implement if Virtualization will be used(hell nah actually)

/* Stateless means that isr can accept state,
but it will only contain EIP CD EFLAGS and int_no, everything else will be zero.
This is necessary in order to reduce interactions with the stack. */

/* 1 Debug */
void isr_1() // marked as stateless in cpu_interrupts.asm
{
    rsod_add_log("ISR1. No debug code provided");
}

/* Breakpoint (int3) */
_Noreturn void isr_3() // marked as stateless in cpu_interrupts.asm
{
    asm volatile("cli");
    while (true)
        ;
}

/* 7 Device Not Available - coprocessor not available */
_Noreturn void isr_7(const cpu_state_t *state)
{
    rsod_add_log("ISR7. Probably FPU/SIMD Not Loaded");
    show_rsod("Coprocessor Not Ready", state, 7);
    __builtin_unreachable();
}
/* 48 Stack Overflow - Custom Stack Guard Exception */
_Noreturn void isr_48(const cpu_state_t *state)
{
    rsod_add_log("[STACK GUARD] Overflow detected!");
    rsod_add_log("ESP below guard limit (0x60000)");
    show_rsod("Stack Overflow", state, 48);
    __builtin_unreachable();
}

void register_all_cpu_exceptions_isrs(void)
{
#define X(n) register_interrupt_handler(n, isr_##n);
    ISR_TO_REGISTRATION
#undef X
}