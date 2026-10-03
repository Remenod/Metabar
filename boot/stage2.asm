; Stage 2: everything the MBR has no room for.
; Collects the BIOS memory map, loads the kernel, enables A20, copies the ramdisk above 1 MiB
; through unreal mode, then switches to protected mode and enters the kernel.

[org 0x7E00]
[bits 16]

KERNEL_OFFSET equ 0x9000 ; on change also update linker.ld and recompile all *.c files
STAGE2_LBA equ 1

E820_MAP equ 0x1000      ; dword count + 20-byte entries; on change also update E820_MAP_ADDR in paging.h
E820_MAX_ENTRIES equ 64  ; on change also update E820_MAX_ENTRIES in paging.h

RAMDISK_INFO equ 0x1600      ; magic, physical base, byte size; on change also update RAMDISK_INFO_ADDR in ramdisk.h
RAMDISK_MAGIC equ 0x4B444D52 ; 'RMDK'
RAMDISK_DEST equ 0x00400000  ; 4 MiB: above the frame bitmap, clear of the kernel's 4 MiB window
BOUNCE_SEGMENT equ 0x3000    ; BIOS can only write below 1 MiB, so sectors land here first
BOUNCE_SECTORS equ 64        ; 32 KiB per round trip

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 12
%endif
%ifndef RAMDISK_SECTORS
%define RAMDISK_SECTORS 0
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

  call enable_a20
  call load_ramdisk

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

; eax = one past the highest usable byte below 4 GiB, taken from the map above
ram_top:
  push ebx
  push ecx
  push esi
  xor eax, eax
  mov cx, [E820_MAP]
  jcxz .done
  mov si, E820_MAP + 4
.next:
  cmp dword [si + 16], 1      ; type: usable RAM
  jne .skip
  cmp dword [si + 4], 0       ; base above 4 GiB is out of reach without PAE
  jne .skip
  cmp dword [si + 12], 0      ; so is a length that crosses it
  jne .skip
  mov ebx, [si]
  add ebx, [si + 8]
  jc .skip
  cmp ebx, eax
  jbe .skip
  mov eax, ebx
.skip:
  add si, 20
  loop .next
.done:
  pop esi
  pop ecx
  pop ebx
  ret

; ===== A20 =====
; ZF clear when memory 1 MiB apart is distinct, i.e. the line is open
a20_enabled:
  push ds
  push es
  push bx
  push si
  push di

  xor ax, ax
  mov es, ax
  mov di, 0x0500              ; scratch in free low memory
  mov ax, 0xFFFF
  mov ds, ax
  mov si, 0x0510              ; 0xFFFF0 + 0x0510 = 0x100500, the same byte when A20 is off

  mov al, [es:di]
  mov bl, al
  mov al, [ds:si]
  mov bh, al

  mov byte [es:di], 0x00
  mov byte [ds:si], 0xFF
  mov al, [es:di]

  mov [ds:si], bh             ; put both bytes back before judging
  mov [es:di], bl
  cmp al, 0xFF                ; our 0x00 turned into 0xFF -> same memory -> A20 off

  pop di
  pop si
  pop bx
  pop es
  pop ds
  ret

; BIOS first, then the fast gate; the kernel tries the 8042 later if both fail
enable_a20:
  call a20_enabled
  jne .done
  mov ax, 0x2401
  int 0x15
  call a20_enabled
  jne .done
  in al, 0x92
  or al, 0x02
  and al, 0xFE                ; bit 0 would reset the CPU
  out 0x92, al
  call a20_enabled
.done:
  ret

; ===== RAMDISK =====
; Copies RAMDISK_SECTORS sectors that sit behind the kernel to RAMDISK_DEST and describes them
; at RAMDISK_INFO. Any problem simply leaves the magic cleared and the kernel boots without it.
load_ramdisk:
  xor eax, eax
  mov [RAMDISK_INFO], eax
%if RAMDISK_SECTORS > 0
  call a20_enabled
  je .done                    ; no A20, no memory above 1 MiB
  call ram_top
  cmp eax, RAMDISK_DEST + RAMDISK_SECTORS * 512
  jb .done                    ; this machine is too small for it

  mov si, msg_loading_ramdisk
  call print_str

  mov eax, STAGE2_LBA + STAGE2_SECTORS + KERNEL_SECTORS
  mov [rd_lba], eax
  mov dword [rd_dest], RAMDISK_DEST
  mov dword [rd_left], RAMDISK_SECTORS ; a 32 MiB ramdisk is already more than 65535 sectors
.chunk:
  mov ecx, [rd_left]
  cmp ecx, BOUNCE_SECTORS
  jbe .count_ok
  mov ecx, BOUNCE_SECTORS
.count_ok:
  push cx
  mov eax, [rd_lba]
  mov dx, BOUNCE_SEGMENT
  call disk_read
  pop cx
  jc .done                    ; half a ramdisk is worse than none

  push cx
  call copy_to_high
  pop cx

  call progress

  movzx eax, cx
  add [rd_lba], eax
  sub [rd_left], eax
  shl eax, 9
  add [rd_dest], eax
  cmp dword [rd_left], 0
  jne .chunk

  call verify_ramdisk
  jne .done                   ; what landed in RAM is not what the disk holds

  mov dword [RAMDISK_INFO + 4], RAMDISK_DEST
  mov dword [RAMDISK_INFO + 8], RAMDISK_SECTORS * 512
  mov dword [RAMDISK_INFO], RAMDISK_MAGIC ; written last: it validates the two fields above
  mov si, msg_done
  call print_str
.done:
%endif
  ret

; Re-reads the first ramdisk sector and compares it with what landed in high memory.
; Catches a gate A20 that only half works and copies that quietly went nowhere. ZF set when equal.
verify_ramdisk:
  mov eax, STAGE2_LBA + STAGE2_SECTORS + KERNEL_SECTORS
  mov cx, 1
  mov dx, BOUNCE_SEGMENT
  call disk_read
  jc .bad

  cli
  call unreal_es
  push ds
  mov ax, BOUNCE_SEGMENT
  mov ds, ax
  xor esi, esi
  mov edi, RAMDISK_DEST
  mov ecx, 512 / 4
  cld
  a32 repe cmpsd
  mov bl, 1
  je .restore
.bad_unreal:
  xor bl, bl
.restore:
  pop ds
  xor ax, ax
  mov es, ax
  sti
  cmp bl, 1
  ret
.bad:
  xor bl, bl
  cmp bl, 1
  ret

; one dot per 2 MiB copied
progress:
  inc byte [rd_chunk]
  cmp byte [rd_chunk], 64
  jb .done
  mov byte [rd_chunk], 0
  push ax
  mov ah, 0x0E
  mov al, '.'
  int 0x10
  pop ax
.done:
  ret

; cx sectors from the bounce buffer to [rd_dest]
; Interrupts stay off for the whole copy: a BIOS handler that reloads ES would drop the 4 GiB
; limit the CPU is caching for it, and the next write above 64 KiB would fault.
copy_to_high:
  cli
  movzx ecx, cx
  shl ecx, 7                  ; sectors * 512 / 4 = dwords
  mov edi, [rd_dest]          ; read it while DS still addresses our variables
  call unreal_es

  push ds
  mov ax, BOUNCE_SEGMENT
  mov ds, ax
  xor esi, esi
  cld                         ; some BIOSes return with the direction flag set
  a32 rep movsd               ; ES carries a 4 GiB limit, so EDI may point above 1 MiB
  pop ds

  xor ax, ax                  ; no BIOS call should see a protected-mode selector in ES
  mov es, ax
  sti
  ret

; Gives ES a 4 GiB limit and returns to real mode without reloading it: the segment register keeps
; the descriptor the CPU cached. int 0x13 reloads ES, so this runs again for every chunk.
unreal_es:
  push eax
  cli
  lgdt [gdt_descriptor]
  mov eax, cr0
  or al, 1
  mov cr0, eax
  jmp $ + 2
  mov ax, DATA_SEG
  mov es, ax
  mov eax, cr0
  and al, 0xFE
  mov cr0, eax
  jmp $ + 2
  pop eax
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
msg_loading_ramdisk db " Loading ramdisk... ", 0
msg_done            db "done!", 0
msg_disk_error      db "Disk read error", 0

rd_lba   dd 0
rd_dest  dd 0
rd_left  dd 0
rd_chunk db 0
