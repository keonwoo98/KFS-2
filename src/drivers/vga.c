#include "vga.h"
#include "io.h"

#define VGA_MEMORY 0xB8000

/* volatile: MMIO — the compiler must not elide or reorder these stores. */
static volatile uint16_t *const g_vga = (volatile uint16_t *)VGA_MEMORY;
static size_t  g_row;
static size_t  g_col;
static uint8_t g_color;

static uint16_t vga_entry(char c, uint8_t color)
{
	return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

void vga_set_color(vga_color_t fg, vga_color_t bg)
{
	g_color = (uint8_t)fg | (uint8_t)((uint8_t)bg << 4);
}

/* Hardware cursor: CRT controller index port 0x3D4, data port 0x3D5.
 * Register 0x0F = cursor location low byte, 0x0E = high byte. */
static void update_cursor(void)
{
	uint16_t pos = (uint16_t)(g_row * VGA_WIDTH + g_col);

	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t)(pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* Register 0x0A (Cursor Start) holds the first scanline of the cell the cursor
 * covers, and bit 5 switches the cursor off; 0x0B (Cursor End) holds the last.
 * On the 16-scanline cell of the default text mode, 14..15 is the usual
 * underline. Written unconditionally rather than read-modify-write: multiboot
 * makes no promise about the CRTC state GRUB leaves behind, and inheriting it
 * is exactly what this function exists to avoid. */
void vga_set_cursor(bool visible)
{
	outb(0x3D4, 0x0A);
	outb(0x3D5, visible ? 14 : 0x20);
	outb(0x3D4, 0x0B);
	outb(0x3D5, 15);
}

void vga_clear(void)
{
	size_t i;

	for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
		g_vga[i] = vga_entry(' ', g_color);
	g_row = 0;
	g_col = 0;
	update_cursor();
}

void vga_init(void)
{
	vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
	vga_clear();
	vga_set_cursor(true);
}

/* Copy rows 1..24 one row up, blank the last row. Explicit loop:
 * memmove on a volatile buffer is not valid C. */
static void scroll(void)
{
	size_t i;

	for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
		g_vga[i] = g_vga[i + VGA_WIDTH];
	for (i = 0; i < VGA_WIDTH; i++)
		g_vga[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = vga_entry(' ', g_color);
	g_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c)
{
	if (c == '\n') {
		g_col = 0;
		g_row++;
	} else if (c == '\r') {
		g_col = 0;
	} else if (c == '\b') {
		/* Cursor only, like a terminal: erasing is the caller's job
		 * ("\b \b"). Keeps this driver free of an erase policy. */
		if (g_col > 0)
			g_col--;
	} else if (c == '\t') {
		g_col = (g_col + 8) & ~(size_t)7;
	} else {
		g_vga[g_row * VGA_WIDTH + g_col] = vga_entry(c, g_color);
		g_col++;
	}
	if (g_col >= VGA_WIDTH) {
		g_col = 0;
		g_row++;
	}
	if (g_row >= VGA_HEIGHT)
		scroll();
	update_cursor();
}

void vga_puts(const char *s)
{
	while (*s)
		vga_putchar(*s++);
}
