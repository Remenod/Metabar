#include <paging/bootstrap/bootstrap_paging.h>

__attribute__((section(".bootstrap"))) void bootstrap_main(void)
{
    asm volatile("cli");
    bootstrap_setup_mapping();
    bootstrap_enable_global_pages();
    bootstrap_enable_paging();
}
