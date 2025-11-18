#include <paging/paging.h>
#include <paging/page_table.h>
#include <paging/page_directory.h>
#include <paging/gdt.h>
#include <lib/arrlib.h>
#include <lib/mem.h>

#include <drivers/qemu_serial.h>

static uint8_t avl_phys_pages_bitmap[TOTAL_FRAMES / 8] = {0};

static gdt_entry_t kernel_gdt[6] = {0};
static gdt_ptr_t gp;

extern void load_page_directory_extern(pde_t page_dir[1024]);

void load_page_directory(pde_t page_dir[1024])
{

    if (page_dir < 0xC0000000)
        load_page_directory_extern(page_dir);
    else
        load_page_directory_extern(vir_to_phys_addr(page_dir));
}

pde_t *kernel_page_directory = NULL;

extern pde_t bootstrap_page_directory[1024];

/* returns PHYSICAL addres of avaible frame */
uint32_t alloc_frame(void)
{
    for (uint32_t i = 0; i < TOTAL_FRAMES; ++i)
    {
        if (!get_bitmap8_val(avl_phys_pages_bitmap, i))
        {
            set_bitmap8_val(avl_phys_pages_bitmap, i, true);
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

void free_frame(uint32_t phys_addr)
{
    set_bitmap8_val(avl_phys_pages_bitmap, phys_addr / PAGE_SIZE, false);
}

/* returns PHYSICAL addres of allocated page table */
uint32_t *alloc_page_table()
{
    uint32_t *pt = alloc_frame();
    for (int i = 0; i < 1024; i++)
    {
        pte_init(&pt[i], NULL, 1, 0, 0, 0, 0, 0, 0);
        pte_set_present_flag(&pt[i], false);
    }
    return pt;
}

/* returns PHYSICAL addres of allocated page directory */
uint32_t *alloc_page_directory()
{
    uint32_t *pt = alloc_frame();
    for (int i = 0; i < 1024; i++)
    {
        pde_init(&pt[i], NULL, 1, 0, 0, 0, 0, 0);
        pde_set_present_flag(&pt[i], false);
    }
    return pt;
}

static void move_stack_to_high_half(void)
{
    uint32_t old_esp, old_ebp;
    asm volatile("mov %%esp, %0" : "=r"(old_esp));
    asm volatile("mov %%ebp, %0" : "=r"(old_ebp));

    uint32_t offset = HIGH_HALF_STACK_TOP - BOOTSTRAP_STACK_TOP;

    // fix frame chain
    uint32_t frame = old_ebp;
    while (frame >= BOOTSTRAP_STACK_BASE && frame <= BOOTSTRAP_STACK_TOP)
    {
        uint32_t *next = (uint32_t *)frame;
        *next += offset;
        frame = *next;
    }

    // copy stack contents
    size_t size = BOOTSTRAP_STACK_TOP - old_esp;
    memmove((void *)(HIGH_HALF_STACK_TOP - size), (void *)old_esp, size);

    // update registers
    asm volatile("mov %0, %%esp" ::"r"(HIGH_HALF_STACK_TOP - size));
    asm volatile("mov %0, %%ebp" ::"r"(old_ebp + offset));
}

static void init_kernel_gdt(void)
{
    asm volatile("sgdt %0" : "=m"(gp));

    memcpy(kernel_gdt, gp.base, gp.limit);

    gp.base = (uint32_t)&kernel_gdt;

    __asm__ volatile(
        "lgdt (%0)\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:\n\t"
        :
        : "r"(&gp)
        : "memory", "ax");
}

void setup_high_half_selfcontained_paging(void)
{
    asm volatile("cli");
    move_stack_to_high_half();
    init_kernel_gdt();

    for (int i = 0; i < KERNEL_PHYS_END / PAGE_SIZE; i++)
        set_bitmap8_val(avl_phys_pages_bitmap, i, true);

    kernel_page_directory = phys_to_vir_addr(alloc_page_directory());

    memcpy(kernel_page_directory, bootstrap_page_directory, PAGE_SIZE);

    kernel_page_directory[0].fields.present = false;

    load_page_directory(kernel_page_directory);
}
