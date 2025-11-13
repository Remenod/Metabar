#include <paging/bootstrap/bootstrap_paging.h>

__attribute__((section(".bootstrap"))) void bootstrap_main()
{
    asm volatile("cli");
    bootstrap_setup_mapping();
    bootstrap_setup_page_directory();
    bootstrap_enable_global_pages();
    bootstrap_enable_paging();
    // bootstrap_transfer_bootstrap_temp_entries();
}
