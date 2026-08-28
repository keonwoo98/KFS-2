#include "shell.h"
#include "libk.h"
#include "keyboard.h"
#include "vga.h"
#include "printk.h"
#include "kernel.h"
#include "io.h"

/* Accumulate with an overflow guard: v * base + d must stay inside 32 bits,
 * which is exactly v <= (UINT32_MAX - d) / base. Checking after the multiply
 * would already have wrapped. */
int shell_parse_u32(const char *s, uint32_t *out)
{
	uint32_t base = 10;
	uint32_t v = 0;
	uint32_t d;
	int      any = 0;

	if (s == NULL || *s == '\0')
		return -1;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	while (*s) {
		if (*s >= '0' && *s <= '9')
			d = (uint32_t)(*s - '0');
		else if (base == 16 && *s >= 'a' && *s <= 'f')
			d = (uint32_t)(*s - 'a' + 10);
		else if (base == 16 && *s >= 'A' && *s <= 'F')
			d = (uint32_t)(*s - 'A' + 10);
		else
			return -1;
		if (v > (0xFFFFFFFFu - d) / base)
			return -1;
		v = v * base + d;
		any = 1;
		s++;
	}
	if (!any)          /* "0x" with no digits after it */
		return -1;
	*out = v;
	return 0;
}

/* In-place: spaces become NULs and argv points into the caller's buffer.
 * Stops at max tokens; anything after that is left unparsed. */
int shell_tokenize(char *line, char **argv, int max)
{
	int argc = 0;

	while (*line) {
		while (*line == ' ')
			*line++ = '\0';
		if (*line == '\0')
			break;
		if (argc == max)
			break;
		argv[argc++] = line;
		while (*line && *line != ' ')
			line++;
	}
	return argc;
}

#define LINE_MAX 128u
#define ARGV_MAX 3

struct cmd {
	const char *name;
	const char *help;
	void (*fn)(int argc, char **argv);
};

/* Forward-declare the handlers so the table can name them, then define the
 * table, then the handlers. cmd_help walks the table, so the two reference
 * each other; this ordering resolves that without a tentative definition of
 * a const array (which is not valid C). Task 5 extends all three blocks. */
static void cmd_help(int argc, char **argv);
static void cmd_stack(int argc, char **argv);
static void cmd_gdt(int argc, char **argv);
static void cmd_dump(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_halt(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);

/* One row per command: adding one in KFS-3 is a single line here. */
static const struct cmd g_cmds[] = {
	{ "help",   "list commands",                  cmd_help   },
	{ "stack",  "hexdump the kernel stack",       cmd_stack  },
	{ "gdt",    "hexdump the GDT at 0x800",       cmd_gdt    },
	{ "dump",   "dump <addr> [len], len 64",      cmd_dump   },
	{ "clear",  "clear the screen",               cmd_clear  },
	{ "halt",   "stop the CPU",                   cmd_halt   },
	{ "reboot", "reset via the 8042",             cmd_reboot },
	{ NULL, NULL, NULL }
};

static void cmd_help(int argc, char **argv)
{
	const struct cmd *c;

	(void)argc;
	(void)argv;
	for (c = g_cmds; c->name; c++)
		printk("  %s -- %s\n", c->name, c->help);
}

static void cmd_stack(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	print_kernel_stack();
}

static void cmd_gdt(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	/* 7 entries * 8 bytes, at the address the subject mandates. */
	dump_hex((const void *)0x00000800, 56);
}

static void cmd_dump(int argc, char **argv)
{
	uint32_t addr;
	uint32_t len = 64;

	if (argc < 2) {
		printk("usage: dump <addr> [len]\n");
		return;
	}
	if (shell_parse_u32(argv[1], &addr) != 0) {
		printk("dump: bad address\n");
		return;
	}
	if (argc >= 3 && shell_parse_u32(argv[2], &len) != 0) {
		printk("dump: bad length\n");
		return;
	}
	/* No validity check on purpose: showing whatever is at an address is
	 * what a debugger is for. Without paging nothing faults anyway. */
	dump_hex((const void *)(uintptr_t)addr, len);
}

static void cmd_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	vga_clear();
}

static void cmd_halt(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printk("halted.\n");
	__asm__ volatile ("cli");
	for (;;)
		__asm__ volatile ("hlt");
}

static void cmd_reboot(int argc, char **argv)
{
	uint32_t i;

	(void)argc;
	(void)argv;
	printk("rebooting...\n");
	outb(0x64, 0xFE);           /* pulse the 8042 reset line */
	for (i = 0; i < 10000000u; i++)
		__asm__ volatile ("pause");
	/* Never leave the machine in an undefined state if the pulse did
	 * nothing: say so and stop. */
	printk("reboot failed; halting\n");
	__asm__ volatile ("cli");
	for (;;)
		__asm__ volatile ("hlt");
}

static void shell_exec(char *line, char **argv)
{
	const struct cmd *c;
	int argc;

	argc = shell_tokenize(line, argv, ARGV_MAX);
	if (argc == 0)
		return;
	for (c = g_cmds; c->name; c++) {
		if (strcmp(c->name, argv[0]) == 0) {
			c->fn(argc, argv);
			return;
		}
	}
	printk("unknown command: %s (try 'help')\n", argv[0]);
}

void shell_run(void)
{
	char   line[LINE_MAX];
	char  *argv[ARGV_MAX];
	size_t len = 0;
	char   c;

	printk("kfs> ");
	for (;;) {
		c = keyboard_poll();
		if (c == 0) {
			/* No hlt here: hlt only wakes on an interrupt, and with
			 * IF=0 and no IDT it would never wake. Polling costs a
			 * busy CPU; pause is the standard spin-loop hint. */
			__asm__ volatile ("pause");
			continue;
		}
		if (c == '\n') {
			vga_putchar('\n');
			line[len] = '\0';
			shell_exec(line, argv);
			len = 0;
			printk("kfs> ");
		} else if (c == '\b') {
			if (len > 0) {
				len--;
				vga_puts("\b \b");
			}
		} else if (len + 1 < LINE_MAX) {
			line[len++] = c;
			vga_putchar(c);
		}
	}
}
