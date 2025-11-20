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

    if ((uint32_t)page_dir < 0xC0000000)
        load_page_directory_extern(page_dir);
    else
        load_page_directory_extern(vir_to_phys_addr(page_dir));
}

pde_t *kernel_page_directory = NULL;

extern pde_t bootstrap_page_directory[1024];

// apply PDE changes in PD
static inline void flush_tlb(void)
{
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" ::"r"(cr3));
}

// apply PDE changes in PD for specific addr
static inline void invlpg(void *addr)
{
    asm volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

// returns PHYSICAL addres of avaible frame
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

// set page that phys addr came from as avaible
void free_frame(uint32_t phys_addr)
{
    set_bitmap8_val(avl_phys_pages_bitmap, phys_addr / PAGE_SIZE, false);
}

// returns PHYSICAL addres of avaible frame
static uint32_t alloc_page_table_phys(void)
{
    return alloc_frame();
}

// returns PHYSICAL addres of avaible frame
static uint32_t alloc_page_directory_phys(void)
{
    return alloc_frame();
}

// returns VIRTUAL addres of current page dir
static inline volatile pde_t *get_pd_virt()
{
    return (volatile pde_t *)0xFFFFF000;
}

// returns VIRTUAL address of page table mapped at given PD index
static inline volatile pte_t *get_pt_virt(uint32_t pd_index)
{
    return (volatile pte_t *)(0xFFC00000 + (pd_index << 12));
}

// returns POINTER to PDE entry for provided virtual address
static inline volatile pde_t *get_pde(uint32_t virt)
{
    return &get_pd_virt()[virt >> 22];
}

// returns POINTER to PTE entry for provided virtual address
static inline volatile pte_t *get_pte(uint32_t virt)
{
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;
    return &get_pt_virt(pd_index)[pt_index];
}

// creates new page table entry in PD and returns VIRTUAL pointer to the PT
volatile pte_t *alloc_page_table_virtual(uint32_t pd_index, uint32_t phys_pt)
{
    volatile pde_t *pd = get_pd_virt();
    pd[pd_index].fields.addr = phys_pt >> 12;
    pd[pd_index].fields.present = 1;
    pd[pd_index].fields.rw = 1;
    pd[pd_index].fields.us = 0;

    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" ::"r"(cr3));

    volatile pte_t *pt = get_pt_virt(pd_index);
    for (int i = 0; i < 1024; i++)
    {
        pt[i].raw_data = 0;
    }
    return pt;
}

// maps given VIRTUAL page to PHYSICAL address with provided flags
void map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    volatile pde_t *pde = get_pde(virt);
    if (!pde->fields.present)
    {
        uint32_t pt_phys = alloc_page_table_phys();
        alloc_page_table_virtual(virt >> 22, pt_phys);
    }

    volatile pte_t *pte = get_pte(virt);
    pte->fields.addr = phys >> 12;
    pte->fields.present = 1;
    pte->fields.rw = (flags & 2) != 0;
    pte->fields.us = (flags & 4) != 0;

    asm volatile("invlpg (%0)" ::"r"(virt));
}

// unmaps given VIRTUAL page if it is present in page table
void unmap_page(uint32_t virt)
{
    volatile pte_t *pte = get_pte(virt);
    if (pte->fields.present)
    {
        pte->raw_data = 0;
        asm volatile("invlpg (%0)" ::"r"(virt));
    }
}

// creates new page directory, initializes self-mapping, returns VIRTUAL PD address
volatile pde_t *create_page_directory(void)
{
    uint32_t phys_pd = alloc_page_directory_phys();

    volatile pde_t *pd_temp = (volatile pde_t *)TEMP_PD_VADDR;

    map_page(TEMP_PD_VADDR, phys_pd, PAGE_PRESENT | PAGE_RW);

    for (int i = 0; i < 1024; i++)
        pd_temp[i].raw_data = 0;

    pd_temp[1023].fields.addr = phys_pd >> 12;
    pd_temp[1023].fields.present = 1;
    pd_temp[1023].fields.rw = 1;

    // asm volatile("mov %0, %%cr3" ::"r"(phys_pd));

    unmap_page(TEMP_PD_VADDR);

    return (volatile pde_t *)0xFFFFF000;
}

static inline void move_stack_to_high_half(void)
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

static inline void init_kernel_gdt(void)
{
    asm volatile("sgdt %0" : "=m"(gp));

    memcpy(kernel_gdt, gp.base, gp.limit);

    gp.base = (gdt_entry_t *)&kernel_gdt;

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

    for (uint32_t i = 0; i < KERNEL_PHYS_END / PAGE_SIZE; i++)
        set_bitmap8_val(avl_phys_pages_bitmap, i, true);

    for (int i = 0xA0000; i <= 0xBFFFF; i++)
        set_bitmap8_val(avl_phys_pages_bitmap, i / PAGE_SIZE, true);

    uint32_t kernel_pd_phys = alloc_page_directory_phys();
    if (!kernel_pd_phys)
        return;

    map_page(TEMP_PD_VADDR, kernel_pd_phys, PAGE_PRESENT | PAGE_RW);

    volatile pde_t *pd_temp = (volatile pde_t *)TEMP_PD_VADDR;

    memcpy((void *)pd_temp, (const void *)bootstrap_page_directory, PAGE_SIZE);

    pd_temp[0].fields.present = 0;

    pd_temp[1023].fields.addr = kernel_pd_phys >> 12;
    pd_temp[1023].fields.present = 1;
    pd_temp[1023].fields.rw = 1;

    flush_tlb();

    unmap_page(TEMP_PD_VADDR);

    load_page_directory_extern((pde_t *)kernel_pd_phys);
}
