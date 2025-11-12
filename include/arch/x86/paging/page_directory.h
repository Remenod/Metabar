#pragma once

#include <lib/types.h>

typedef struct
{
    union
    {
        struct
        {
            uint32_t present : 1;  // 0 = not present, 1 = present
            uint32_t rw : 1;       // 0 = read only, 1 = read/write
            uint32_t us : 1;       // 0 = kernel, 1 = user
            uint32_t pwt : 1;      // Write-through
            uint32_t pcd : 1;      // Cache disable
            uint32_t accessed : 1; // CPU sets on access
            uint32_t ps : 1;       // Page size: 0 = 4 KiB, 1 = 4 MiB
            uint32_t reserved : 1; // reserved
            uint32_t avl : 4;      // available for OS
            uint32_t addr : 20;    // [31:12] physical address (aligned)
        } fields;
        uint32_t raw_data;
    };

} pde_t;

/* addr format is addrs from 0x00000000 to 0xFFFFF000 range */
inline void pde_set_addr(pde_t *entry, uint32_t phys_addr) { entry->fields.addr = phys_addr >> 12; }

/* set flags (lower 12 bits) */
inline void pde_set_flags(pde_t *entry, uint16_t flags) { entry->raw_data = (entry->raw_data & 0xFFFFF000) | (flags & 0x0FFF); }

/* Present
 true = present | false = not present */
inline void pde_set_present_flag(pde_t *entry, bool_t val) { entry->fields.present = val != 0; };

/* ReadWrite
 true = read/write | false = readonly */
inline void pde_set_rw_flag(pde_t *entry, bool_t val) { entry->fields.rw = val != 0; }

/* User/Supervisor
false = ring 0 only | true = allow ring 3 */
inline void pde_set_us_flag(pde_t *entry, bool_t val) { entry->fields.us = val != 0; }

/* Page-level Write-Through
false = Write-back(more effective) | true = Write-through(direct ram write) */
inline void pde_set_pwt_flag(pde_t *entry, bool_t val) { entry->fields.pwt = val != 0; }

/* Page-level Cache Disable - used for MMIO
false = caching | true = no caching */
inline void pde_set_pcd_flag(pde_t *entry, bool_t val) { entry->fields.pcd = val != 0; }

/* User/Supervisor
false = ring 0 only | true = allow ring 3 */
inline void pde_set_ps_flag(pde_t *entry, bool_t val) { entry->fields.ps = val != 0; }

/* Available for OS - any info. CPU will ignore that
Any 4(!) bits*/
inline void pde_set_avl_flag(pde_t *entry, uint8_t val) { entry->fields.avl = (val & 0xF); }

inline uint8_t pde_get_avl_flag(pde_t *entry) { return entry->fields.avl; }
inline bool_t pde_get_accesed_flag(pde_t *entry) { return entry->fields.accessed; }

/* addr may be in 0x00000000 to 0xFFFFF000 range */
inline void pde_init(pde_t *entry, uint32_t addr, bool_t rw, bool_t us, bool_t pwt, bool_t pcd, bool_t ps, uint8_t avl)
{
    pde_set_addr(entry, addr);
    pde_set_rw_flag(entry, rw);
    pde_set_us_flag(entry, us);
    pde_set_pwt_flag(entry, pwt);
    pde_set_pcd_flag(entry, pcd);
    pde_set_ps_flag(entry, ps);
    pde_set_avl_flag(entry, avl);
    pde_set_present_flag(entry, 1);
    entry->fields.accessed = 0;
    entry->fields.reserved = 0;
}