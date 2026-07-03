#include "kernel.h"
#include "vga.h"
#include "printk.h"

void kernel_main(uint32_t magic, uint32_t mb_info_addr)
{
	vga_init();
	if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
		printk("PANIC: bad multiboot magic: 0x%x\n", magic);
		return;
	}
	if (selftest_run() == 0)
		printk("kfs: selftest ok\n");
	/* after selftest: scroll_exercise would push this banner off screen */
	printk("kfs: multiboot ok (magic 0x%x, mbi 0x%x)\n", magic, mb_info_addr);
	printk("ptest [%c|%s|%d|%d|%u|%x|%%|%08x|%02x]\n",
		'X', "str", -42, -2147483647 - 1, 4294967295u, 0xdeadbeef,
		0xc0ffee, 0xf);
	vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
	printk("\n42\n");
}
