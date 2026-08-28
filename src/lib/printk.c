#include "printk.h"
#include "vga.h"
#include "types.h"

/* Width is emitted as leading pad characters, so buf only ever holds
 * the digits of a 32-bit value (max 10 for base 10) — no overflow. */
static void print_number(char sign, uint32_t n, uint32_t base, int width, char pad)
{
	static const char digits[] = "0123456789abcdef";
	char   buf[32];
	int    i = 0;
	int    total;

	if (n == 0)
		buf[i++] = '0';
	while (n > 0) {
		buf[i++] = digits[n % base];
		n /= base;
	}
	total = i + (sign ? 1 : 0);
	/* zero-pad puts the sign before the fill ("-0042"); space-pad puts
	 * the fill before the sign ("  -42") -- matches standard printf. */
	if (sign && pad == '0')
		vga_putchar(sign);
	while (width > total) {
		vga_putchar(pad);
		width--;
	}
	if (sign && pad != '0')
		vga_putchar(sign);
	while (i-- > 0)
		vga_putchar(buf[i]);
}

static void print_signed(int32_t v, int width, char pad)
{
	if (v < 0)
		print_number('-', -(uint32_t)v, 10, width, pad); /* INT_MIN-safe */
	else
		print_number(0, (uint32_t)v, 10, width, pad);
}

/* Pointers print as a full 32-bit address ("0x00001234"): the width is
 * fixed at 8 hex digits so there is no ambiguity about the address size,
 * which is why an explicit field width is ignored for %p. */
static void print_ptr(uintptr_t p)
{
	vga_puts("0x");
	print_number(0, (uint32_t)p, 16, 8, '0');
}

void printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	while (*fmt) {
		char pad;
		int  width;

		if (*fmt != '%') {
			vga_putchar(*fmt++);
			continue;
		}
		fmt++; /* skip '%' */
		pad = ' ';
		width = 0;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}
		if (width > 31)
			width = 31;
		if (*fmt == '\0')
			break;
		if (*fmt == 'c') {
			/* char is promoted to int through varargs; width ignored */
			vga_putchar((char)va_arg(ap, int));
		} else if (*fmt == 's') {
			const char *s = va_arg(ap, const char *);

			vga_puts(s ? s : "(null)"); /* width ignored for %s */
		} else if (*fmt == 'd') {
			print_signed(va_arg(ap, int32_t), width, pad);
		} else if (*fmt == 'u') {
			print_number(0, va_arg(ap, uint32_t), 10, width, pad);
		} else if (*fmt == 'x') {
			print_number(0, va_arg(ap, uint32_t), 16, width, pad);
		} else if (*fmt == 'p') {
			print_ptr((uintptr_t)va_arg(ap, void *)); /* width ignored */
		} else if (*fmt == '%') {
			vga_putchar('%');
		} else {
			/* unsupported directive: echo it (width chars are lost) */
			vga_putchar('%');
			vga_putchar(*fmt);
		}
		fmt++;
	}
	va_end(ap);
}
