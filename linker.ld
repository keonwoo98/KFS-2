ENTRY(_start)

SECTIONS
{
	/* GRUB loads the kernel at 1 MiB (conventional, above BIOS/VGA areas). */
	. = 1M;

	/* Multiboot header must be within the first 8192 bytes of the image. */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.text)
	}

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata*)
	}

	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	}

	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	}
}
