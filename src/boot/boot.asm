; Multiboot v1 header + kernel entry point.
; GRUB scans the first 8192 bytes for this header, loads us at 1 MiB
; (see linker.ld), switches to 32-bit protected mode and jumps to _start
; with eax = 0x2BADB002 and ebx = physical address of the multiboot info.

MBALIGN  equ 1 << 0                  ; align loaded modules on page boundaries
MEMINFO  equ 1 << 1                  ; provide memory map in multiboot info
MBFLAGS  equ MBALIGN | MEMINFO
MAGIC    equ 0x1BADB002              ; multiboot v1 header magic
CHECKSUM equ -(MAGIC + MBFLAGS)      ; magic + flags + checksum must be 0

section .multiboot
align 4
	dd MAGIC
	dd MBFLAGS
	dd CHECKSUM

section .bss
align 16
stack_bottom:
	resb 16384                       ; 16 KiB kernel stack
stack_top:

section .text
global _start
extern kernel_main

_start:
	mov esp, stack_top               ; C needs a stack; GRUB gives none
	sub esp, 8                       ; +2 pushes +ret = entry esp 16-aligned (psABI)
	cld                              ; SysV ABI expects DF cleared
	push ebx                         ; arg 2: multiboot info address
	push eax                         ; arg 1: multiboot magic
	call kernel_main

.hang:                               ; kernel_main returned: halt forever
	cli
	hlt
	jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
