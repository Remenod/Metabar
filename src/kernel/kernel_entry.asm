section .bootstrap

[BITS 32]
[GLOBAL _start]
extern bootstrap_main
extern kernel_main

_start:
    call bootstrap_main ; runs on the bootloader stack (0x9FFFC), enables high-half paging

    ; switch to the high-half kernel stack here, outside of any C function:
    ; changing ESP from C code breaks the compiler's own stack bookkeeping
    mov esp, kernel_stack_top
    xor ebp, ebp
    call kernel_main
    jmp $

; lives in .bss, so its frames are below __phys_after_kernel and get reserved with the kernel
section .bss nobits alloc noexec write align=4096
global kernel_stack
global kernel_stack_top
kernel_stack:
    resb 0x40000
kernel_stack_top:
