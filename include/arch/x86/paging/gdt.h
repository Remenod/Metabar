#pragma once

#include <lib/types.h>

typedef enum __attribute__((packed))
{
    tss_16_avl = 1,
    ldt = 2,
    tss_16_busy = 3,
    call_gate_16 = 4,
    task_gate = 5,
    int_gate_16 = 6,
    trap_gate_16 = 7,
    tss_32_avl = 8,
    tss_32_busy = 10,
    call_gate_32 = 11,
    int_gate_32 = 12,
    trap_gate_32 = 13
} system_descriptor_type_t;

typedef struct
{
    uint8_t present : 1;
    uint8_t cpl : 2;
    // should be 0
    uint8_t desc_type : 1;
    system_descriptor_type_t type : 4;
} system_access_byte_t __attribute__((packed));

typedef struct
{
    uint8_t present : 1;
    uint8_t cpl : 2;
    // should be 1
    uint8_t desc_type : 1;
    // should be 0
    uint8_t datacode_desc_type : 1;
    uint8_t conforming : 1;
    uint8_t readable : 1;
    uint8_t accessed : 1;
} code_access_byte_t __attribute__((packed));

typedef struct
{
    uint8_t present : 1;
    uint8_t cpl : 2;
    // should be 1
    uint8_t desc_type : 1;
    // should be 1
    uint8_t datacode_desc_type : 1;
    uint8_t expand_down : 1;
    uint8_t writeable : 1;
    uint8_t accessed : 1;
} data_access_byte_t __attribute__((packed));

typedef struct
{
    // 16 bits
    uint16_t limit_low;
    // 16 bits
    uint16_t base_low;
    // 8 bits
    uint8_t base_mid;
    union
    {
        system_access_byte_t system;
        code_access_byte_t code;
        data_access_byte_t data;
    } access;
    /*
    4 bits
    1 - Granularity
    1 - Default/Big (0 - 16bit, 1 - 32bit)
    0 - 64bit
    0 - Available for use by system software
    */
    uint8_t flags : 4;
    // 4 bits
    uint8_t limit_high : 4;
    // 8 bits
    uint8_t base_high;
} gdt_entry_t __attribute__((packed));

typedef struct
{
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t __attribute__((packed));

void gdte_set_limit(gdt_entry_t *inst, uint32_t lim);
void gdte_set_base(gdt_entry_t *inst, uint32_t base);
void gdte_init_code(gdt_entry_t *inst, uint32_t base, uint32_t limit, bool_t conforming, bool_t readable);
void gdte_init_data(gdt_entry_t *inst, uint32_t base, uint32_t limit, bool_t expand_down, bool_t writeable);
void gdte_init_system(gdt_entry_t *inst, uint32_t base, uint32_t limit, system_descriptor_type_t type);