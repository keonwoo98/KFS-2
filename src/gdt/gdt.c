#include "gdt.h"

/* Build the table directly at GDT_BASE. The linker cannot place it there:
 * GRUB (multiboot) refuses to load ELF segments below 1 MiB, so the table
 * is written at runtime instead. volatile: the only reader is the CPU via
 * lgdt inside the opaque asm routine, so the stores must never be elided. */
static volatile struct gdt_entry *const gdt =
	(volatile struct gdt_entry *)GDT_BASE;
static struct gdt_ptr gp;

/* gdt_flush.asm: lgdt, reload DS/ES/FS/GS/SS, far-jump to reload CS. */
void gdt_flush(uint32_t gdt_ptr_addr);

/* GCC -O2 misclassifies a pointer cast from an integer constant as a
 * zero-size object; the writes are in-range (idx 0..GDT_ENTRIES-1). */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow="
static void gdt_set_gate(int idx, uint32_t base, uint32_t limit,
			 uint8_t access, uint8_t gran)
{
	gdt[idx].limit_low   = limit & 0xFFFF;
	gdt[idx].base_low    = base & 0xFFFF;
	gdt[idx].base_middle = (base >> 16) & 0xFF;
	gdt[idx].access      = access;
	/* upper 4 bits (G/D/L/AVL) from gran, lower 4 = limit 19:16 */
	gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
	gdt[idx].base_high   = (base >> 24) & 0xFF;
}
#pragma GCC diagnostic pop

void gdt_init(void)
{
	/* All segments are flat: base 0, limit 0xFFFFF pages (4 GiB).
	 * access: P|DPL|S|type -> 0x9A code, 0x92 data, +0x60 for DPL=3. */
	gdt_set_gate(0, 0, 0x00000, 0x00, 0x00); /* null (CPU requirement) */
	gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF); /* kernel code  */
	gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF); /* kernel data  */
	gdt_set_gate(3, 0, 0xFFFFF, 0x92, 0xCF); /* kernel stack */
	gdt_set_gate(4, 0, 0xFFFFF, 0xFA, 0xCF); /* user code    */
	gdt_set_gate(5, 0, 0xFFFFF, 0xF2, 0xCF); /* user data    */
	gdt_set_gate(6, 0, 0xFFFFF, 0xF2, 0xCF); /* user stack   */
	gp.limit = sizeof(struct gdt_entry) * GDT_ENTRIES - 1; /* 55 */
	gp.base  = GDT_BASE;
	gdt_flush((uint32_t)&gp);
}
