#pragma once

#include <lib/types.h>

typedef struct
{
    uint32_t EBP;
    uint32_t EBX;
    uint32_t ESI;
    uint32_t EDI;
    uint32_t ESP;
    uint32_t EIP;
} jmp_buf_t;

int setjmp(jmp_buf_t *buf);

_Noreturn void longjmp(jmp_buf_t *buf, int arg);
