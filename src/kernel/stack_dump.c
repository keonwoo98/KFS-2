#include "kernel.h"
#include "printk.h"
#include "types.h"

/* Top of the 16 KiB kernel stack reserved in boot.asm's .bss.
 * x86 stacks grow downward: esp starts at stack_top and decreases,
 * so [esp, stack_top) is exactly the data currently on the stack. */
extern uint8_t stack_top[];

/* One row: "AAAAAAAA  XX " * 16 "|" ascii "|" = 76 cols (fits 80). */
void dump_hex(const void *addr, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)addr;
	uint32_t       i;
	uint32_t       j;
	uint8_t        c;

	for (i = 0; i < len; i += 16) {
		printk("%08x  ", (uint32_t)(uintptr_t)(p + i));
		for (j = 0; j < 16; j++) {
			if (i + j < len)
				printk("%02x ", p[i + j]);
			else
				printk("   ");
		}
		printk("|");
		for (j = 0; j < 16 && i + j < len; j++) {
			c = p[i + j];
			printk("%c", (c >= 0x20 && c <= 0x7e) ? (char)c : '.');
		}
		printk("|\n");
	}
}

void print_kernel_stack(void)
{
	uint32_t esp;
	uint32_t top;
	uint32_t start;

	__asm__ volatile ("mov %%esp, %0" : "=r"(esp));
	top   = (uint32_t)(uintptr_t)stack_top;
	start = esp & ~(uint32_t)0xf; /* align rows to 16 for readability */
	printk("kernel stack: esp=%08x top=%08x size=%u bytes\n",
		esp, top, top - esp);
	dump_hex((const void *)start, top - start);
}
