#include "kernel.h"
#include "gdt.h"
#include "vga.h"
#include "printk.h"
#include "keyboard.h"

void kernel_main(uint32_t magic, uint32_t mb_info_addr)
{
	/* volatile: force a real stack slot so the dump provably contains
	 * a known value (02 b0 ad 2b little-endian) for the boot test. */
	volatile uint32_t stack_canary;

	vga_init();
	if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
		printk("PANIC: bad multiboot magic: 0x%x\n", magic);
		return;
	}
	gdt_init();
	if (selftest_run() == 0)
		printk("kfs: selftest ok\n");
	/* after selftest: scroll_exercise would push this banner off screen */
	printk("kfs: multiboot ok (magic 0x%x, mbi 0x%x)\n", magic, mb_info_addr);
	printk("ptest [%c|%s|%d|%d|%u|%x|%p|%%|%08x|%02x]\n",
		'X', "str", -42, -2147483647 - 1, 4294967295u, 0xdeadbeef,
		(void *)0x1234, 0xc0ffee, 0xf);
	stack_canary = MULTIBOOT_BOOTLOADER_MAGIC;
	(void)stack_canary;
	print_kernel_stack();
	vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
	printk("\n42\n");
	/* TEMPORARY: replaced by shell_run() in Task 4. Present so that Task 2's
	 * sendkey probe has something that visibly reacts to a keystroke. */
	keyboard_init();
	for (;;) {
		char c = keyboard_poll();

		if (c != 0)
			vga_putchar(c);
		__asm__ volatile ("pause");
	}
}
