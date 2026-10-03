#include <paging/paging.h>
#include <paging/page_table.h>
#include <paging/page_directory.h>
#include <paging/gdt.h>
#include <lib/arrlib.h>
#include <lib/mem.h>
#include <kernel/ramdisk.h>

// one bit per frame of usable RAM (1 = used), sized from E820 and placed above 1 MiB by init_frame_bitmap
static uint8_t *avl_phys_pages_bitmap = NULL;
static uint32_t last_avl_frame_index = 0;

// one past the highest frame of usable RAM, frames above it do not exist and are not in the bitmap
static uint32_t frames_limit = 0;

static void set_alv_frame(uint32_t index, bool_t val)
{
    if (index >= frames_limit)
        return;
    if (!val)
    {
        if (index < last_avl_frame_index)
            last_avl_frame_index = index;
    }
    set_bitmap8_val(avl_phys_pages_bitmap, index, val);
}
static bool_t get_alv_frame(uint32_t index)
{
    return get_bitmap8_val(avl_phys_pages_bitmap, index);
}

/* marks frames of [base, base + length) as used or free, clamped to the frames the bitmap covers
 * a free range is shrunk to the whole frames inside it, a used range is grown over partial frames */
static void set_alv_frame_range(uint64_t base, uint64_t length, bool_t used)
{
    uint64_t first = used ? base >> 12 : (base + PAGE_SIZE - 1) >> 12;
    uint64_t end = used ? (base + length + PAGE_SIZE - 1) >> 12 : (base + length) >> 12;

    if (end > frames_limit)
        end = frames_limit;

    for (uint64_t i = first; i < end; i++)
        set_alv_frame(i, used);
}

/* whole frames of a usable E820 entry below 4 GiB as [*first, *end)
 * returns false for reserved entries and for ones with no whole frame below 4 GiB (no PAE) */
static bool_t e820_usable_frames(const volatile e820_entry_t *entry, uint32_t *first, uint32_t *end)
{
    if (entry->type != E820_TYPE_USABLE)
        return false;

    uint64_t f = (entry->base + PAGE_SIZE - 1) >> 12;
    uint64_t e = (entry->base + entry->length) >> 12;
    if (e > TOTAL_FRAMES)
        e = TOTAL_FRAMES;
    if (f >= e)
        return false;

    *first = f;
    *end = e;
    return true;
}

/* Builds the frame bitmap from the BIOS E820 map: a frame is available only if the BIOS reports it as usable RAM.
 * The bitmap covers usable RAM only and lives in the first usable frames above 1 MiB that the kernel window maps,
 * so low memory stays free for the kernel image. Needs A20 and the bootstrap identity mapping (the E820 map is in
 * low memory). Returns false if there is no map, no usable RAM or no room above 1 MiB for the bitmap. */
static bool_t init_frame_bitmap(void)
{
    const uint32_t count = *(volatile uint32_t *)E820_MAP_ADDR;
    const volatile e820_entry_t *map = (const volatile e820_entry_t *)(E820_MAP_ADDR + 4);
    uint32_t first, end;

    if (count == 0 || count > E820_MAX_ENTRIES)
        return false;

    frames_limit = 0;
    for (uint32_t i = 0; i < count; i++)
        if (e820_usable_frames(&map[i], &first, &end) && end > frames_limit)
            frames_limit = end;
    if (frames_limit == 0)
        return false;

    const uint32_t bitmap_bytes = ((frames_limit + 7) / 8 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    const uint32_t bitmap_frames = bitmap_bytes / PAGE_SIZE;
    const uint32_t window_end = (KERNEL_PHYS_BASE + KERNEL_WINDOW_SIZE) / PAGE_SIZE;

    // lowest usable spot above 1 MiB that is still inside the kernel window
    uint32_t bitmap_frame = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if (!e820_usable_frames(&map[i], &first, &end))
            continue;
        if (first < HIGH_MEM_START / PAGE_SIZE)
            first = HIGH_MEM_START / PAGE_SIZE;
        if (end > window_end)
            end = window_end;
        // the ramdisk is already sitting in RAM, the bitmap must not land on top of it
        const uint32_t rd_first = ramdisk_phys_base() / PAGE_SIZE;
        const uint32_t rd_end = (ramdisk_phys_base() + ramdisk_size()) / PAGE_SIZE;
        if (ramdisk_size() != 0 && first < rd_end && rd_first < first + bitmap_frames)
            first = rd_end;

        if (first + bitmap_frames <= end && (bitmap_frame == 0 || first < bitmap_frame))
            bitmap_frame = first;
    }
    if (bitmap_frame == 0)
        return false;

    avl_phys_pages_bitmap = phys_to_vir_addr(bitmap_frame * PAGE_SIZE);
    memset(avl_phys_pages_bitmap, 0xFF, bitmap_bytes);

    for (uint32_t i = 0; i < count; i++)
        if (map[i].type == E820_TYPE_USABLE)
            set_alv_frame_range(map[i].base, map[i].length, false);

    // entries may overlap: a frame reserved by any entry stays reserved
    for (uint32_t i = 0; i < count; i++)
        if (map[i].type != E820_TYPE_USABLE)
            set_alv_frame_range(map[i].base, map[i].length, true);

    // kernel image, its .bss and the kernel stack; frame 0 is also alloc_frame's "no frame" value
    set_alv_frame_range(0, KERNEL_PHYS_END, true);
    set_alv_frame_range(LOW_MEM_RESERVED_START, LOW_MEM_RESERVED_END - LOW_MEM_RESERVED_START, true);
    set_alv_frame_range((uint64_t)bitmap_frame * PAGE_SIZE, bitmap_bytes, true);
    set_alv_frame_range(ramdisk_phys_base(), ramdisk_size(), true); // loaded by stage 2, not ours to hand out

    last_avl_frame_index = 0;
    return true;
}

static gdt_entry_t kernel_gdt[8] = {0};
static gdt_ptr_t gp;

extern void load_page_directory_extern(pde_t page_dir[1024]);

extern pde_t bootstrap_page_directory[1024];

// apply PDE changes in PD
static inline void flush_tlb(void)
{
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" ::"r"(cr3));
}

// returns PHYSICAL addres of avaible frame
uint32_t alloc_frame(void)
{
    for (uint32_t i = last_avl_frame_index; i < frames_limit; ++i)
    {
        if (!get_alv_frame(i))
        {
            set_alv_frame(i, true);
            last_avl_frame_index = i + 1;
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

/* Allocates `pages` frames that are next to each other in physical memory, with the first one
 * aligned to `align` bytes (a power of two, rounded up to a page). Devices need this for DMA.
 * Returns the physical address of the first frame, or 0 if there is no such run. */
uint32_t alloc_contiguous_frames(uint32_t pages, uint32_t align)
{
    if (pages == 0 || pages > frames_limit)
        return 0;

    uint32_t step = align > PAGE_SIZE ? align / PAGE_SIZE : 1;
    uint32_t start = last_avl_frame_index;
    start = (start + step - 1) / step * step;

    while (start + pages <= frames_limit)
    {
        uint32_t i = start;
        while (i < start + pages && !get_alv_frame(i))
            ++i;

        if (i == start + pages)
        {
            for (uint32_t j = start; j < start + pages; ++j)
                set_alv_frame(j, true);
            if (start + pages > last_avl_frame_index)
                last_avl_frame_index = start + pages;
            return start * PAGE_SIZE;
        }

        // frame i is taken, so the next run can only start after it
        start = (i + 1 + step - 1) / step * step;
    }

    return 0;
}

// set page that phys addr came from as avaible
void free_frame(uint32_t phys_addr)
{
    set_alv_frame(phys_addr / PAGE_SIZE, false);
}

void free_frames(uint32_t phys_addr, uint32_t pages)
{
    for (uint32_t i = 0; i < pages; ++i)
        set_alv_frame(phys_addr / PAGE_SIZE + i, false);
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
// returns false if a new page table was needed and no frame was available for it
bool_t map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    volatile pde_t *pde = get_pde(virt);
    if (!pde->fields.present)
    {
        uint32_t pt_phys = alloc_page_table_phys();
        if (!pt_phys)
            return false;
        alloc_page_table_virtual(virt >> 22, pt_phys);
    }

    volatile pte_t *pte = get_pte(virt);
    pte->fields.addr = phys >> 12;
    pte->fields.present = 1;
    pte->fields.rw = (flags & 2) != 0;
    pte->fields.us = (flags & 4) != 0;

    asm volatile("invlpg (%0)" ::"r"(virt));
    return true;
}

/* PHYSICAL address a virtual one currently maps to, or 0 if the page is not mapped.
 * Works for any address of the current page directory, unlike vir_to_phys_addr,
 * which only knows the linear kernel window. */
uint32_t virt_to_phys(uint32_t virt)
{
    if (!get_pde(virt)->fields.present)
        return 0;

    volatile pte_t *pte = get_pte(virt);
    if (!pte->fields.present)
        return 0;

    return (pte->fields.addr << 12) | (virt & 0xFFF);
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

bool_t setup_high_half_selfcontained_paging(void)
{
    asm volatile("cli");
    init_kernel_gdt();

    if (!init_frame_bitmap())
        return false;

    uint32_t kernel_pd_phys = alloc_page_directory_phys();
    if (!kernel_pd_phys)
        return false;

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

    // the kernel window's page table is shared with the bootstrap PD, so this removes the page from both
    unmap_page((uint32_t)kernel_stack_guard);

    return true;
}
