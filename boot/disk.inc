; Sector reader shared by both boot stages (%include'd into each, so every stage has its own copy).
; The caller must set boot_drive before calling disk_init.
; Reads go through int 0x13 extensions (LBA); without them it falls back to CHS, one sector per call.

SECTORS_PER_READ equ 64 ; 32 KiB per int 0x13 call, below the 127-sector limit of many BIOSes

; probes the drive once: LBA extensions if present, otherwise CHS geometry
; returns with CF set if the geometry could not be read
disk_init:
  mov ah, 0x41                ; extensions present?
  mov bx, 0x55AA
  mov dl, [boot_drive]
  int 0x13
  jc .chs
  cmp bx, 0xAA55
  jne .chs
  test cl, 1                  ; packet (LBA) access supported
  jz .chs
  mov byte [use_lba], 1
  mov dword [chs_capacity], 0xFFFFFFFF
  clc
  ret
.chs:
  mov byte [use_lba], 0
  mov ah, 0x08                ; drive geometry
  mov dl, [boot_drive]
  xor di, di                  ; ES:DI = 0 works around buggy BIOSes
  int 0x13
  jc .fail
  push cx
  and cl, 0x3F
  mov [sectors_per_track], cl
  inc dh
  mov [heads], dh

  pop cx
  mov al, ch
  mov ah, cl
  shr ah, 6
  inc ax                      ; cylinder count
  movzx eax, ax
  movzx bx, byte [heads]
  mul bx                      ; dx:ax = cylinders * heads
  shl edx, 16
  mov dx, ax
  mov eax, edx
  movzx ebx, byte [sectors_per_track]
  mul ebx
  mov [chs_capacity], eax
  clc
  ret
.fail:
  stc
  ret

; reads sectors into a segment, offset 0; CF set on a read error
;   eax = first LBA, cx = sector count, dx = destination segment
disk_read:
  mov [dap.lba], eax
  mov [dap.segment], dx
  mov [sectors_left], cx
.next:
  mov ax, [sectors_per_read]
  cmp ax, [sectors_left]
  jbe .count_ok
  mov ax, [sectors_left]
.count_ok:
  mov [dap.count], ax
  cmp byte [use_lba], 0
  je .read_chs
  mov si, dap
  mov ah, 0x42
  jmp .read
.read_chs:
  ; LBA -> CHS in 32 bits: the ramdisk sits far beyond the few thousand sectors of the kernel
  mov eax, [dap.lba]
  xor edx, edx
  movzx ebx, byte [sectors_per_track]
  div ebx                     ; eax = LBA / spt, edx = sector - 1
  mov cl, dl
  inc cl                      ; sector numbers start at 1
  xor edx, edx
  movzx ebx, byte [heads]
  div ebx                     ; eax = cylinder, edx = head
  mov dh, dl
  mov ch, al
  shr ax, 2
  and al, 0xC0
  or cl, al                   ; cylinder bits 8-9 go to CL bits 6-7

  ; one call may cover the rest of the track, which beats reading sector by sector
  movzx ax, byte [sectors_per_track]
  movzx bx, cl
  and bx, 0x3F
  sub ax, bx
  inc ax                      ; sectors left on this track
  cmp ax, [dap.count]
  jbe .chs_track_ok
  mov ax, [dap.count]
.chs_track_ok:
  push ax

  ; floppy reads go through ISA DMA, which cannot cross a 64 KiB physical boundary
  mov ax, [dap.segment]
  shl ax, 4
  neg ax                      ; bytes left below the next boundary, 0 means a full 64 KiB
  shr ax, 9
  jnz .chs_dma_ok
  mov ax, 128
.chs_dma_ok:
  pop bx
  cmp ax, bx
  jbe .chs_count_ok
  mov ax, bx
.chs_count_ok:
  mov [dap.count], ax
  les bx, [dap.offset]        ; ES:BX = buffer
  mov ah, 0x02
.read:
  mov dl, [boot_drive]
  int 0x13
  jc .fail
  mov ax, [dap.count]
  add [dap.lba], ax
  mov bx, ax
  shl bx, 5                   ; sectors * 512 / 16 = paragraphs
  add [dap.segment], bx       ; every chunk starts at offset 0 of its own segment, so a read never wraps
  sub [sectors_left], ax
  jnz .next
  clc
  ret
.fail:
  stc
  ret

boot_drive db 0

sectors_left dw 0
sectors_per_read dw SECTORS_PER_READ
use_lba db 0
sectors_per_track db 0
heads db 0
chs_capacity dd 0             ; one past the last sector CHS can reach

dap:                          ; int 0x13 AH=0x42 disk address packet
  db 0x10, 0                  ; packet size, reserved
.count:   dw 0
.offset:  dw 0
.segment: dw 0
.lba:     dq 0
