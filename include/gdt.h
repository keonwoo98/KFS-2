#ifndef GDT_H
# define GDT_H

# include "types.h"

/* The subject requires the GDT at this physical address. It sits in the
 * real-mode free area (0x500-0x7BFF), well below the kernel at 1 MiB. */
# define GDT_BASE       0x00000800
# define GDT_ENTRIES    7

/* Selector = index * 8 (TI=0: GDT, RPL=0). */
# define GDT_SEL_KCODE  0x08
# define GDT_SEL_KDATA  0x10
# define GDT_SEL_KSTACK 0x18
# define GDT_SEL_UCODE  0x20
# define GDT_SEL_UDATA  0x28
# define GDT_SEL_USTACK 0x30

/* One 8-byte segment descriptor, exactly as the CPU expects it.
 * base/limit are split for 286->386 backward compatibility. */
struct gdt_entry {
	uint16_t limit_low;    /* limit 15:0  */
	uint16_t base_low;     /* base  15:0  */
	uint8_t  base_middle;  /* base  23:16 */
	uint8_t  access;       /* P | DPL(2) | S | type(4) */
	uint8_t  granularity;  /* G | D | L | AVL | limit 19:16 */
	uint8_t  base_high;    /* base  31:24 */
} __attribute__((packed));

/* Operand of lgdt: size-1 and linear address of the table. */
struct gdt_ptr {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

void gdt_init(void);

#endif
