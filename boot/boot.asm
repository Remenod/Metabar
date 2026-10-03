[org 0x7c00]
STAGE2_OFFSET equ 0x7E00 ; right behind this sector, below the kernel at 0x9000
STAGE2_LBA equ 1

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif

[bits 16]

start:
  cli
  xor ax, ax
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov sp, 0x7C00
  sti

  mov [boot_drive], dl
  mov ax, 0x0003     ; 03h
  int 0x10

  call disk_init
  jc disk_error

  mov eax, STAGE2_LBA
  mov cx, STAGE2_SECTORS
  mov dx, STAGE2_OFFSET >> 4
  call disk_read
  jc disk_error

  mov dl, [boot_drive]
  jmp 0:STAGE2_OFFSET

disk_error:
  mov si, msg_disk_error
  call print_str
  jmp $

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

; ===== STRINGS =====
msg_disk_error db "Disk read error", 0

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
