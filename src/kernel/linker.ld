ENTRY(_start)

SECTIONS
{
    . = 0x9000;

    .bootstrap :
    {
        KEEP(*(.bootstrap))
        KEEP(*(.bootstrap.*))
    }
    __phys_after_bootstrap = ALIGN(. , 0x1000);

    .bootstrap_rodata : AT(ADDR(.bootstrap) + SIZEOF(.bootstrap))
    {
        KEEP(*(.bootstrap_rodata))
        KEEP(*(.bootstrap_rodata.*))
    }
    __phys_after_bootstrap_data = ALIGN(. , 0x1000);

    .text 0xC0000000 : AT(__phys_after_bootstrap_data)
    {
        *(.text)
        *(.text.*)
    }

    .rodata 0xC0000000 + SIZEOF(.text) : AT(__phys_after_bootstrap_data + SIZEOF(.text))
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data 0xC0000000 + SIZEOF(.text) + SIZEOF(.rodata) : AT(__phys_after_bootstrap_data + SIZEOF(.text) + SIZEOF(.rodata))
    {
        *(.data)
        *(.data.*)
    }

    .bss 0xC0000000 + SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) : AT(__phys_after_bootstrap_data + SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data))
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    PROVIDE(_phys_after_bootstrap = __phys_after_bootstrap);
    PROVIDE(_phys_after_bootstrap_data = __phys_after_bootstrap_data);
}
