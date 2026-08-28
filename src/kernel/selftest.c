#include "libk.h"
#include "vga.h"
#include "kernel.h"
#include "gdt.h"
#include "keyboard.h"

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

/* Verify the GDT the CPU is actually using: read GDTR back with sgdt,
 * check the live segment selectors, and compare the kernel-code
 * descriptor bytes at 0x808 against the expected encoding. */
static void test_gdt(void)
{
	struct gdt_ptr gdtr;
	uint16_t       sel;
	int            before = g_failed;
	static const uint8_t kcode[8] = {
		0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00
	};

	__asm__ volatile ("sgdt %0" : "=m"(gdtr));
	check(gdtr.base == GDT_BASE, "gdtr base 0x800");
	/* cast: avoid -Wsign-compare (uint16_t vs size_t) under -Werror */
	check(gdtr.limit == (uint16_t)(sizeof(struct gdt_entry) * GDT_ENTRIES - 1),
		"gdtr limit 55");
	/* CS==0x08 and DS==0x10 coincide with the selectors GRUB leaves us
	 * on entry, so these checks do NOT prove our far jump / data reload
	 * ran. Only SS==0x18 does -- GRUB never uses 0x18 -- so it is the
	 * sole proof of a real segment reload. Do not remove the SS check. */
	__asm__ volatile ("mov %%cs, %0" : "=r"(sel));
	check(sel == GDT_SEL_KCODE, "cs selector");
	__asm__ volatile ("mov %%ds, %0" : "=r"(sel));
	check(sel == GDT_SEL_KDATA, "ds selector");
	__asm__ volatile ("mov %%ss, %0" : "=r"(sel));
	check(sel == GDT_SEL_KSTACK, "ss selector");
	check(memcmp((const void *)(GDT_BASE + 8), kcode, 8) == 0,
		"kernel code descriptor bytes");
	if (g_failed == before)
		vga_puts("kfs: gdt ok\n");
}

/* Pure table lookup: no hardware, no screen output. Silent on success. */
static void test_keyboard(void)
{
	check(kbd_translate(0x1E, false) == 'a',  "kbd a");
	check(kbd_translate(0x1E, true)  == 'A',  "kbd shift a");
	check(kbd_translate(0x02, false) == '1',  "kbd 1");
	check(kbd_translate(0x02, true)  == '!',  "kbd shift 1");
	check(kbd_translate(0x1C, false) == '\n', "kbd enter");
	check(kbd_translate(0x0E, false) == '\b', "kbd backspace");
	check(kbd_translate(0x39, false) == ' ',  "kbd space");
	check(kbd_translate(0x3B, false) == 0,    "kbd f1 unmapped");
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
	test_gdt();
	test_keyboard();
	return g_failed;
}
