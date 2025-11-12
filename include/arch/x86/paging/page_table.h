#pragma once

#include <lib/types.h>

typedef struct
{
    union
    {
        struct
        {
            uint32_t present : 1;  // 0 = not present, 1 = present
            uint32_t rw : 1;       // 0 = read-only, 1 = read/write
            uint32_t us : 1;       // 0 = kernel, 1 = user
            uint32_t pwt : 1;      // write-through
            uint32_t pcd : 1;      // cache disable
            uint32_t accessed : 1; // set by CPU on access
            uint32_t dirty : 1;    // set by CPU on write
            uint32_t pat : 1;      // PAT bit (Page Attribute Table)
            uint32_t global : 1;   // global page (TLB not flushed)
            uint32_t avl : 3;      // available for OS
            uint32_t addr : 20;    // [31:12] physical frame address
        } fields;
        uint32_t raw_data;
    };
} pte_t;

/* set physical address */
inline void pte_set_addr(pte_t *entry, uint32_t phys_addr) { entry->fields.addr = phys_addr >> 12; }

/* set flags (lower 12 bits) */
inline void pte_set_flags(pte_t *entry, uint16_t flags) { entry->raw_data = (entry->raw_data & 0xFFFFF000) | (flags & 0x0FFF); }

/* Present
 true = present | false = not present */
inline void pte_set_present_flag(pte_t *entry, bool_t val) { entry->fields.present = val != 0; };

/* ReadWrite
 true = read/write | false = readonly */
inline void pte_set_rw_flag(pte_t *entry, bool_t val) { entry->fields.rw = val; }

/* User/Supervisor
false = ring 0 only | true = allow ring 3 */
inline void pte_set_us_flag(pte_t *entry, bool_t val) { entry->fields.us = val; }

/* Page-level Write-Through
false = Write-back(more effective) | true = Write-through(direct ram write) */
inline void pte_set_pwt_flag(pte_t *entry, bool_t val) { entry->fields.pwt = val; }

/* Page-level Cache Disable - used for MMIO
false = caching | true = no caching */
inline void pte_set_pcd_flag(pte_t *entry, bool_t val) { entry->fields.pcd = val; }

/* Global Page
false = page will be flushed from TLB on CR3 reload | true  = page stays in TLB (requires CR4.PGE) */
inline void pte_set_global_flag(pte_t *entry, bool_t val) { entry->fields.global = val; }

/* Available for OS - any info. CPU will ignore that
Any 3(!) bits*/
inline void pte_set_avl_flag(pte_t *entry, uint8_t val) { entry->fields.avl = val & 0x7; }

inline uint8_t pte_get_avl_flag(pte_t *entry) { return entry->fields.avl; }
inline bool_t pte_get_accessed(pte_t *entry) { return entry->fields.accessed; }
inline bool_t pte_get_dirty(pte_t *entry) { return entry->fields.dirty; }

/* init */
inline void pte_init(pte_t *entry, uint32_t addr, bool_t rw, bool_t us, bool_t pwt, bool_t pcd, bool_t pat, bool_t global, uint8_t avl)
{
    pte_set_addr(entry, addr);
    pte_set_rw_flag(entry, rw);
    pte_set_us_flag(entry, us);
    pte_set_pwt_flag(entry, pwt);
    pte_set_pcd_flag(entry, pcd);
    pte_set_global_flag(entry, global);
    pte_set_avl_flag(entry, avl);
    pte_set_present_flag(entry, 1);
    entry->fields.accessed = 0;
    entry->fields.dirty = 0;
    entry->fields.pat = 0;
}