#include <interrupts/idt.h>
#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <timer/pit.h>
#include <drivers/vga.h>
#include <interrupts/cpu_exceptions.h>
#include <paging/paging.h>
#include <a20.h>
#include <kernel/ramdisk.h>
#include <kernel/diagnostics/warning_routine.h>
#include <kernel/settings.h>
#include <kernel/memory.h>
#include "../../apps/app_selector/app_selector.h"

_Noreturn static void halt(void)
{
    for (;;)
        asm volatile("cli; hlt");
}

void kernel_main()
{
    asm volatile("cli");

    const char done_text[] = "Done\n";

    ramdisk_read_boot_info(); // while low memory is still identity mapped

    // before paging setup: the frame bitmap goes above 1 MiB
    print("Enabling A20... ");
    if (!a20_enable())
    {
        print("Failed: A20 line stays disabled\n");
        halt();
    }
    print(done_text);

    print("Kernel Page Dir Initialization... ");
    if (!setup_high_half_selfcontained_paging())
    {
        print("Failed: not enough usable RAM (E820)\n");
        halt();
    }
    print(done_text);

    print("Mapping ramdisk... ");
    if (ramdisk_size() == 0)
        print("none\n");
    else if (ramdisk_map())
    {
        print_dec(ramdisk_sectors());
        print(" sectors\n");
    }
    else
        print("Failed\n");

    print("Setting Initialization... ");
    settings_init();
    print(done_text);

    print("Installing IDT... ");
    idt_install();
    print(done_text);

    print("PIT Initialization... ");
    pit_init(settings_get_int("timer.frequency", 1000));
    print(done_text);

    print("CPU int registration... ");
    register_all_cpu_exceptions_isrs();
    print(done_text);

    print("Heap Initialization... ");
    heap_init();
    print(done_text);

    print("Installing mouse... ");
    mouse_install();
    print(done_text);

    print("Installing keyboard... ");
    keyboard_install();
    print(done_text);

    print("Calibtating kernel warning loop sleep... ");
    init_kernel_warning_routine();
    print(done_text);

    print("Testing VGA modes... ");
    set_graphics_mode();
    draw_mode13h_test_pattern();
    set_text_mode();
    print(done_text);

    clear_screen();

    app_selector();
}
