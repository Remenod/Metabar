[org 0x7c00]
KERNEL_OFFSET equ 0x9000 ; on change also update linker.ld and recompile all *.c files
E820_MAP equ 0x1000      ; dword count + 20-byte entries; on change also update E820_MAP_ADDR in paging.h
E820_MAX_ENTRIES equ 64  ; on change also update E820_MAX_ENTRIES in paging.h
SECTORS_PER_READ equ 64  ; 32 KiB per int 0x13 call, below the 127-sector limit of many BIOSes

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 12
%endif

[bits 16]

start:
  cli
  xor ax, ax
  mov ds, ax
  mov es, ax

  ;mov ax, 0x0013     ; 13h — 320x200x256
  ;int 0x10

  mov ax, 0x0003     ; 03h
  int 0x10

  mov [BOOT_DRIVE], dl
  mov bp, 0x9000
  mov sp, bp

  call detect_memory

  mov si, msg_loading_kernel
  call print_str

  call load_kernel

  mov si, msg_kernel_loaded
  call print_str

  call switch_to_pm
  jmp $

disk_error:
  mov si, msg_disk_error
  call print_str
  jmp $

; BIOS memory map (int 0x15, EAX=0xE820) -> E820_MAP
; count stays 0 if the BIOS does not support it, the kernel checks that
detect_memory:
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

; reads KERNEL_SECTORS sectors starting at LBA 1 to KERNEL_OFFSET, one int 0x13 call per chunk
; uses int 0x13 extensions (LBA), without them falls back to CHS, one sector per call
load_kernel:
  mov ah, 0x41                ; extensions present?
  mov bx, 0x55AA
  mov dl, [BOOT_DRIVE]
  int 0x13
  jc .chs
  cmp bx, 0xAA55
  jne .chs
  test cl, 1                  ; packet (LBA) access supported
  jnz .next
.chs:
  mov byte [sectors_per_read], 1
  mov ah, 0x08                ; drive geometry
  mov dl, [BOOT_DRIVE]
  xor di, di                  ; ES:DI = 0 works around buggy BIOSes
  int 0x13
  jc disk_error
  and cl, 0x3F
  mov [sectors_per_track], cl
  inc dh
  mov [heads], dh
.next:
  mov ax, [sectors_per_read]
  cmp ax, [sectors_left]
  jbe .count_ok
  mov ax, [sectors_left]
.count_ok:
  mov [dap.count], ax
  cmp byte [sectors_per_read], 1
  je .read_chs
  mov si, dap
  mov ah, 0x42
  jmp .read
.read_chs:
  ; LBA -> CHS; linker.ld keeps the kernel below 0x9F000, so LBA < 1202 and every quotient fits in 8 bits
  mov ax, [dap.lba]
  div byte [sectors_per_track] ; al = LBA / spt, ah = LBA % spt
  mov cl, ah
  inc cl                      ; sector numbers start at 1
  xor ah, ah
  div byte [heads]            ; al = cylinder, ah = head
  mov ch, al
  mov dh, ah
  les bx, [dap.offset]        ; ES:BX = buffer
  mov ax, 0x0201
.read:
  mov dl, [BOOT_DRIVE]
  int 0x13
  jc disk_error
  mov ax, [dap.count]
  add [dap.lba], ax
  mov bx, ax
  shl bx, 5                   ; sectors * 512 / 16 = paragraphs
  add [dap.segment], bx       ; next chunk starts at offset 0 of a new segment, so a read never wraps
  sub [sectors_left], ax
  jnz .next
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

; ====Access byte guide====
; 1    - Present (P) - 1 for active sector
; 00   - Descriptor Privilege Level
; 1    - Descriptor type (S) - system(0), code or data(1)
;
;   Descriptor type 1 only
; 1    - Data/Code - Data(0), Code(1)
; 0    - Expand-down(Stack) data/Conforming code(1)
; 1    - Readonly data/Executeonly code(0), Writable data/Readable code(1)
; 0    - Accessed (A) - 0 by default, 1 set by CPU by accesing Descriptor
; =========================

; =======Flags guide=======
; 1    - Granularity
; 1    - Default/Big (0 - 16bit, 1 - 32bit)
; 0    - 64bit
; 0    - Available for use by system software
; 1111 - limit high
; =========================

; ===== GDT =====
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

  ;mov ax, TEST_SEG
  ;mov gs, ax
  
  mov esp, 0x9FFFC

  call KERNEL_OFFSET
  jmp $

; ===== STRINGS =====
msg_loading_kernel db "Loading kernel... ", 0
msg_kernel_loaded  db "Kernel loaded!", 0
msg_disk_error     db "Disk read error", 0

BOOT_DRIVE db 0

sectors_left dw KERNEL_SECTORS
sectors_per_read dw SECTORS_PER_READ
sectors_per_track db 0
heads db 0

dap:                          ; int 0x13 AH=0x42 disk address packet
  db 0x10, 0                  ; packet size, reserved
.count:   dw 0
.offset:  dw 0
.segment: dw KERNEL_OFFSET >> 4
.lba:     dq 1                ; kernel starts right after the boot sector

times 446 - ($ - $$) db 0

; ===== Partition Table (64 bytes) =====
; One active partition starting at LBA=1, size — 100 sectors

db 0x80                    ; status — active
db 0x01, 0x01, 0x00        ; CHS start (doesn't matter, set to minimum)
db 0x06                    ; partition type (FAT16, or any valid one)
db 0xFE, 0xFF, 0xFF        ; CHS end (maximum — for reliability)
dd 0x00000001              ; LBA start = 1 (after MBR)
dd 0x00000064              ; size = 100 sectors (can be changed)

times 16 * 3 db 0          ; the remaining 3 records are zero

; ===== Boot Signature =====
dw 0xAA55
