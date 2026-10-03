#include <paging/bootstrap/bootstrap_paging.h>

#include <lib/types.h>

extern uint8_t __phys_bss_start; // from linker script
extern uint8_t __phys_bss_end;

__attribute__((section(".bootstrap"))) static void bootstrap_zero_bss(void)
{
    volatile uint8_t *byte = (volatile uint8_t *)&__phys_bss_start;
    const volatile uint8_t *end = (const volatile uint8_t *)&__phys_bss_end;

    while (byte < end)
        *byte++ = 0;
}

__attribute__((section(".bootstrap"))) void bootstrap_main(void)
{
    asm volatile("cli");
    bootstrap_zero_bss();
    bootstrap_setup_mapping();
    bootstrap_enable_global_pages();
    bootstrap_enable_paging();
}
