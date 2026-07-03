; Load our GDT and reload every segment register.
; lgdt only fills the GDTR register; each segment register keeps a hidden
; descriptor cache that is refreshed only when the register is reloaded.
; CS cannot be written with mov -- only a far jump reloads it.

bits 32

section .text
global gdt_flush

gdt_flush:                     ; cdecl: gdt_flush(uint32_t gdt_ptr_addr)
	mov  eax, [esp + 4]
	lgdt [eax]                 ; GDTR <- {limit=55, base=0x800}
	mov  ax, 0x10              ; kernel data selector
	mov  ds, ax
	mov  es, ax
	mov  fs, ax
	mov  gs, ax
	mov  ax, 0x18              ; kernel stack selector (base 0: esp stays valid)
	mov  ss, ax
	jmp  0x08:.flush           ; far jump reloads CS with kernel code
.flush:
	ret                        ; stack untouched -> plain return works

section .note.GNU-stack noalloc noexec nowrite progbits
