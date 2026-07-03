#include "printk.h"
#include "vga.h"
#include "types.h"

/* Width is emitted as leading pad characters, so buf only ever holds
 * the digits of a 32-bit value (max 10 for base 10) — no overflow. */
static void print_unsigned(uint32_t n, uint32_t base, int width, char pad)
{
	static const char digits[] = "0123456789abcdef";
	char   buf[32];
	int    i = 0;

	if (n == 0)
		buf[i++] = '0';
	while (n > 0) {
		buf[i++] = digits[n % base];
		n /= base;
	}
	while (width > i) {
		vga_putchar(pad);
		width--;
	}
	while (i-- > 0)
		vga_putchar(buf[i]);
}

static void print_signed(int32_t v, int width, char pad)
{
	uint32_t u;

	if (v < 0) {
		vga_putchar('-');
		u = -(uint32_t)v; /* unsigned negation: INT_MIN-safe */
		if (width > 0)
			width--;   /* '-' counts toward the field width */
	} else {
		u = (uint32_t)v;
	}
	print_unsigned(u, 10, width, pad);
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
			print_unsigned(va_arg(ap, uint32_t), 10, width, pad);
		} else if (*fmt == 'x') {
			print_unsigned(va_arg(ap, uint32_t), 16, width, pad);
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
