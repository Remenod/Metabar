#include <interrupts/idt.h>

#include <ports.h>
#include <interrupts/pic.h>

struct idt_entry
{
    uint16_t base_low;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define ISR_LIST X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9)            \
    X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20)         \
        X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)     \
            X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) \
                X(43) X(44) X(45) X(46) X(47) X(48)

#define X(n) extern void isr##n();
ISR_LIST
#undef X

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

void idt_set_gate(int num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

extern void lidt(void *); // asm-func

void idt_install()
{
    remap_pic();

    idtp.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idtp.base = (uint32_t)&idt;

#define X(n) idt_set_gate(n, (uint32_t)isr##n, 0x08, 0x8E);
    ISR_LIST
#undef X

    lidt(&idtp);
}
