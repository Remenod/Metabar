#include <paging/gdt.h>

void gdte_set_limit(gdt_entry_t *inst, uint32_t lim)
{
    inst->limit_low = lim & 0x0FFFF;
    inst->limit_high = (lim & 0xF0000) >> 16;
}
void gdte_set_base(gdt_entry_t *inst, uint32_t base)
{
    inst->base_low = base & 0x0000FFFF;
    inst->base_mid = (base & 0x00FF0000) >> 16;
    inst->base_high = (base & 0xFF000000) >> 24;
}

void gdte_init_code(gdt_entry_t *inst, uint32_t base, uint32_t limit, bool_t conforming, bool_t readable)
{
    gdte_set_limit(inst, limit);
    gdte_set_base(inst, base);
    inst->access.code.present = 1;
    inst->access.code.cpl = 0;
    inst->access.code.desc_type = 1;
    inst->access.code.datacode_desc_type = 0;
    inst->access.code.conforming = conforming;
    inst->access.code.readable = readable;
}

void gdte_init_data(gdt_entry_t *inst, uint32_t base, uint32_t limit, bool_t expand_down, bool_t writeable)
{
    gdte_set_limit(inst, limit);
    gdte_set_base(inst, base);
    inst->access.data.present = 1;
    inst->access.data.cpl = 0;
    inst->access.data.desc_type = 1;
    inst->access.data.datacode_desc_type = 1;
    inst->access.data.expand_down = expand_down;
    inst->access.data.writeable = writeable;
}

void gdte_init_system(gdt_entry_t *inst, uint32_t base, uint32_t limit, system_descriptor_type_t type)
{
    gdte_set_limit(inst, limit);
    gdte_set_base(inst, base);
    inst->access.system.present = 1;
    inst->access.system.cpl = 0;
    inst->access.system.desc_type = 0;
    inst->access.system.type = type;
}