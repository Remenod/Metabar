; Stage 2: everything the MBR has no room for.
; Collects the BIOS memory map, loads the kernel, then switches to protected mode and enters it.

[org 0x7E00]
[bits 16]

KERNEL_OFFSET equ 0x9000 ; on change also update linker.ld and recompile all *.c files
STAGE2_LBA equ 1

E820_MAP equ 0x1000      ; dword count + 20-byte entries; on change also update E820_MAP_ADDR in paging.h
E820_MAX_ENTRIES equ 64  ; on change also update E820_MAX_ENTRIES in paging.h

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 12
%endif
stage2:
  cld
  xor ax, ax          ; stage 1 leaves ES pointing at its last read destination
  mov ds, ax
  mov es, ax
  mov [boot_drive], dl

  call disk_init
  jc disk_error

  call detect_memory

  mov si, msg_loading_kernel
  call print_str

  mov eax, STAGE2_LBA + STAGE2_SECTORS
  mov cx, KERNEL_SECTORS
  mov dx, KERNEL_OFFSET >> 4
  call disk_read
  jc disk_error

  mov si, msg_kernel_loaded
  call print_str

  jmp switch_to_pm

disk_error:
  mov si, msg_disk_error
  call print_str
  jmp $

; ===== BIOS MEMORY MAP (int 0x15, EAX=0xE820) -> E820_MAP =====
; count stays 0 if the BIOS does not support it, the kernel checks that
detect_memory:
  xor ax, ax                  ; the BIOS writes the entries through ES:DI
  mov es, ax
  xor ebx, ebx                ; continuation value, 0 = first entry
  mov [E820_MAP], ebx
  mov di, E820_MAP + 4
.next:
  mov eax, 0xE820
  mov edx, 0x534D4150         ; 'SMAP'
  mov ecx, 20                 ; base, length, type; ACPI 3.0 attributes are ignored, like Linux does
  int 0x15
  jc .done                    ; unsupported or past the last entry
  cmp eax, 0x534D4150
  jne .done
  inc dword [E820_MAP]
  add di, 20
  cmp di, E820_MAP + 4 + 20 * E820_MAX_ENTRIES
  jae .done
  test ebx, ebx               ; 0 = that was the last entry
  jnz .next
.done:
  ret

; ===== PRINT TO SCREEN =====
print_str:
  pusha
  mov ah, 0x0E
.print_char:
  lodsb
  cmp al, 0
  je .done
  int 0x10
  jmp .print_char
.done:
  popa
  ret

%include "boot/disk.inc"

; ===== GDT =====
; keep the layout: the kernel copies this table in init_kernel_gdt and the Segment Test app shows it
gdt_start:
  dq 0x0

gdt_code:
  dw 0xffff         ; limit low (16 bit)
  dw 0x0000         ; base low (16 bit)
  db 0x00           ; base mid (8 bit)
  db 10011010b      ; access byte
  db 11001111b      ; flags + limit high (4 bit)
  db 0x00           ; base high (8 bit)

gdt_data:
  dw 0xffff         ; limit low (16 bit)
  dw 0x0000         ; base low (16 bit)
  db 0x00           ; base mid (8 bit)
  db 10010010b      ; access byte
  db 11001111b      ; flags + limit high (4 bit)
  db 0x00           ; base high (8 bit)

gdt_stack:
  dw 0x004f         ; limit low (16 bit)
  dw 0x0000         ; base low (16 bit)
  db 0x00           ; base mid (8 bit)
  db 10010110b      ; access byte
  db 11000000b      ; flags + limit high (4 bit)
  db 0x00           ; base high (8 bit)

gdt_test:
  dw 0x7df0         ; limit low (16 bit)
  dw 0x0000         ; base low (16 bit)
  db 0x00           ; base mid (8 bit)
  db 00010110b      ; access byte
  db 11001111b      ; flags + limit high (4 bit)
  db 0x00           ; base high (8 bit)

gdt_end:

gdt_descriptor:
  dw gdt_end - gdt_start - 1
  dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start
STACK_SEG equ gdt_stack - gdt_start
TEST_SEG equ gdt_test - gdt_start

switch_to_pm:
  cli
  lgdt [gdt_descriptor]
  mov eax, cr0
  or eax, 0x1
  mov cr0, eax
  jmp CODE_SEG:init_pm

[bits 32]

init_pm:
  mov ax, STACK_SEG ; unused
  mov ss, ax

  mov ax, DATA_SEG
  mov gs, ax
  mov es, ax
  mov ds, ax
  mov fs, ax

  mov esp, 0x7C00   ; below the boot sector, far away from the EBDA

  call KERNEL_OFFSET
  jmp $

; ===== STRINGS =====
msg_loading_kernel  db "Loading kernel... ", 0
msg_kernel_loaded   db "Kernel loaded!", 0
msg_disk_error      db "Disk read error", 0
