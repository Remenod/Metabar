#include <paging/paging.h>
#include <paging/page_table.h>
#include <paging/page_directory.h>
#include <paging/gdt.h>
#include <lib/arrlib.h>
#include <lib/mem.h>

#include <drivers/qemu_serial.h>

static uint8_t avl_phys_pages_bitmap[TOTAL_FRAMES / 8] = {0};
static uint32_t last_avl_frame_index = 0;

static void set_alv_frame(uint32_t index, bool_t val)
{
    if (!val)
    {
        if (index < last_avl_frame_index)
            last_avl_frame_index = index;
    }
    set_bitmap8_val(avl_phys_pages_bitmap, index, val);
}
static bool_t get_alv_frame(uint32_t index)
{
    bool_t val = get_bitmap8_val(avl_phys_pages_bitmap, index);
    if (!val)
    {
        if (index < last_avl_frame_index)
            last_avl_frame_index = index;
    }

    return val;
}

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
    for (uint32_t i = last_avl_frame_index; i < TOTAL_FRAMES; ++i)
    {
        if (!get_alv_frame(i))
        {
            set_alv_frame(i, true);
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

/*
 * Quick search for N contiguous frames.
 * Returns the physical address of the first frame (phys = start_frame * PAGE_SIZE),
 * or 0 if not found.
 */
uint32_t alloc_contiguous_frames(uint32_t pages)
{
    if (pages == 0 || pages > TOTAL_FRAMES)
        return 0;

    uint32_t run = 0;
    uint32_t start = 0;

    for (uint32_t i = last_avl_frame_index; i < TOTAL_FRAMES; ++i)
    {
        if (!get_alv_frame(i))
        {
            if (run == 0)
                start = i;
            ++run;
            if (run >= pages)
            {
                for (uint32_t j = start; j < start + pages; ++j)
                    set_alv_frame(j, true);
                return start * PAGE_SIZE;
            }
        }
        else
        {
            run = 0;
        }
    }

    for (uint32_t i = 0; i < last_avl_frame_index; ++i)
    {
        if (!get_alv_frame(i))
        {
            if (run == 0)
                start = i;
            ++run;
            if (run >= pages)
            {
                for (uint32_t j = start; j < start + pages; ++j)
                    set_alv_frame(j, true);
                return start * PAGE_SIZE;
            }
        }
        else
        {
            run = 0;
        }
    }

    return 0;
}

// set page that phys addr came from as avaible
void free_frame(uint32_t phys_addr)
{
    set_alv_frame(phys_addr / PAGE_SIZE, false);
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

/* * Batch mapping: map_range(virt_start, phys_start, pages, flags)
 * - Does NOT call invlpg for each page
 * - Allocates the page table only once per PD index
 * - Calls flush_tlb() once at the end */
void map_range(uint32_t virt_start, uint32_t phys_start, uint32_t pages, uint32_t flags)
{
    if (pages == 0)
        return;

    uint32_t virt = virt_start;
    uint32_t phys = phys_start;

    uint32_t pages_left = pages;
    while (pages_left > 0)
    {
        uint32_t pd_index = virt >> 22;
        uint32_t pt_index = (virt >> 12) & 0x3FF;

        uint32_t chunk = 1024 - pt_index;
        if (chunk > pages_left)
            chunk = pages_left;

        volatile pde_t *pde = get_pd_virt() + pd_index;
        if (!pde->fields.present)
        {
            uint32_t pt_phys = alloc_page_table_phys();
            if (!pt_phys)
            {
                flush_tlb();
                return;
            }
            alloc_page_table_virtual(pd_index, pt_phys);
        }

        volatile pte_t *pt = get_pt_virt(pd_index);

        for (uint32_t i = 0; i < chunk; ++i)
        {
            uint32_t idx = pt_index + i;
            pt[idx].fields.addr = (phys >> 12);
            pt[idx].fields.present = 1;
            pt[idx].fields.rw = (flags & 2) != 0;
            pt[idx].fields.us = (flags & 4) != 0;

            phys += PAGE_SIZE;
            virt += PAGE_SIZE;
            --pages_left;
        }
    }

    flush_tlb();
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
        set_alv_frame(i, true);

    for (int i = 0xA0000; i <= 0xBFFFF; i++)
        set_alv_frame(i / PAGE_SIZE, true);

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
