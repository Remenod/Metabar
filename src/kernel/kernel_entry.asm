section .bootstrap

[BITS 32]
[GLOBAL _start]
extern bootstrap_main
extern kernel_main

_start:
    call bootstrap_main
    call kernel_main
    jmp $
