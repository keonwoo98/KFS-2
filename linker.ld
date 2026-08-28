ENTRY(_start)

SECTIONS
{
	/* GRUB loads the kernel at 1 MiB (conventional, above BIOS/VGA areas). */
	. = 1M;

	/* Multiboot header must be within the first 8192 bytes of the image. */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.text .text.*)
	}

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata .rodata.*)
	}

	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data .data.*)
	}

	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss .bss.*)
	}

	/* Keep the loaded image to exactly the four sections above. Unwind
	 * tables and toolchain banners are dead weight in a kernel. Do NOT
	 * discard .note.GNU-stack: ld derives the non-executable PT_GNU_STACK
	 * program header from it. */
	/DISCARD/ :
	{
		*(.eh_frame .eh_frame.*)
		*(.comment)
	}
}
