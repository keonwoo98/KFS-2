#include "shell.h"
#include "libk.h"
#include "keyboard.h"
#include "vga.h"
#include "printk.h"

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

/* One row per command: adding one in KFS-3 is a single line here. */
static const struct cmd g_cmds[] = {
	{ "help", "list commands", cmd_help },
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
