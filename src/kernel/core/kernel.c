#include <interrupts/idt.h>
#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <timer/pit.h>
#include <drivers/vga.h>
#include <interrupts/cpu_exceptions.h>
#include <paging/paging.h>
#include <kernel/diagnostics/stack_guard/stack_guard.h>
#include <kernel/diagnostics/warning_routine.h>
#include <kernel/settings.h>
#include "../../apps/app_selector/app_selector.h"

#include <drivers/qemu_serial.h>

void kernel_main()
{
    asm volatile("cli");

    const char done_text[] = "Done\n";

    print("Kernel Page Dir Initialization... ");
    setup_high_half_selfcontained_paging();
    print(done_text);

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

    print("Installing mouse... ");
    mouse_install();
    print(done_text);

    print("Installing keyboard... ");
    keyboard_install();
    print(done_text);

    // print("Calibtating kernel warning loop sleep... ");
    // init_kernel_warning_routine();
    // print(done_text);

    // print("Installing Stack Guard... ");
    // stack_guard_install();
    // print(done_text);

    // print("Testing VGA modes... ");
    // set_graphics_mode();
    // draw_mode13h_test_pattern();
    // set_text_mode();
    // print(done_text);

    clear_screen();

    app_selector();
}
