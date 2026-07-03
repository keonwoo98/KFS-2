#include "libk.h"
#include "vga.h"
#include "kernel.h"

static int g_failed;

static void check(int ok, const char *name)
{
	if (!ok) {
		vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
		vga_puts("SELFTEST FAIL: ");
		vga_puts(name);
		vga_putchar('\n');
		vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
		g_failed++;
	}
}

static void test_mem(void)
{
	char buf[8];

	memset(buf, 'A', 8);
	check(buf[0] == 'A' && buf[7] == 'A', "memset fill");
	memcpy(buf, "1234567", 8);
	check(buf[0] == '1' && buf[6] == '7' && buf[7] == '\0', "memcpy");
	check(memcmp("abc", "abc", 3) == 0, "memcmp equal");
	check(memcmp("abd", "abc", 3) > 0, "memcmp greater");
	check(memcmp("abb", "abc", 3) < 0, "memcmp less");
	memmove(buf + 1, buf, 6);
	check(buf[0] == '1' && buf[1] == '1' && buf[6] == '6',
		"memmove fwd overlap");
	memcpy(buf, "1234567", 8);
	memmove(buf, buf + 1, 6);
	check(buf[0] == '2' && buf[5] == '7' && buf[6] == '7',
		"memmove bwd overlap");
}

static void test_str(void)
{
	check(strlen("") == 0, "strlen empty");
	check(strlen("42") == 2, "strlen 42");
	check(strcmp("42", "42") == 0, "strcmp equal");
	check(strcmp("a", "b") < 0, "strcmp less");
	check(strcmp("b", "a") > 0, "strcmp greater");
	check(strcmp("ab", "abc") < 0, "strcmp prefix");
}

/* Print 30 numbered lines: forces the 25-row screen to scroll.
 * boot_test asserts SCRL29 visible, SCRL00 scrolled away. */
static void scroll_exercise(void)
{
	char line[8] = "SCRL00\n";
	int  i;

	for (i = 0; i < 30; i++) {
		line[4] = (char)('0' + i / 10);
		line[5] = (char)('0' + i % 10);
		vga_puts(line);
	}
}

int selftest_run(void)
{
	g_failed = 0;
	scroll_exercise(); /* first: FAIL lines must survive the scrolling */
	test_mem();
	test_str();
	return g_failed;
}
