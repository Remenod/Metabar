#pragma once

#include <lib/types.h>

#define PIC1_COMMAND 0x20
#define PIC2_COMMAND 0xA0
#define PIC1_DATA 0x21
#define PIC_EOI 0x20

#define IDT_ENTRIES 256

/* cpu state dump for RSOD
do NOT swap the fields
*/
typedef struct cpu_state
{
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t err_code;
    uint32_t eip, cs, eflags;
} cpu_state_t;
typedef void (*isr_t)(const cpu_state_t *state);

void register_interrupt_handler(uint32_t int_no, isr_t handler);
