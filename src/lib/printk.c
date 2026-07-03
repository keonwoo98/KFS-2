#include "printk.h"
#include "vga.h"
#include "types.h"

static void print_unsigned(uint32_t n, uint32_t base)
{
	static const char digits[] = "0123456789abcdef";
	char   buf[32];
	size_t i = 0;

	if (n == 0) {
		vga_putchar('0');
		return;
	}
	while (n > 0) {
		buf[i++] = digits[n % base];
		n /= base;
	}
	while (i-- > 0)
		vga_putchar(buf[i]);
}

static void print_signed(int32_t v)
{
	uint32_t u;

	if (v < 0) {
		vga_putchar('-');
		u = -(uint32_t)v; /* unsigned negation: INT_MIN-safe */
	} else {
		u = (uint32_t)v;
	}
	print_unsigned(u, 10);
}

void printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	while (*fmt) {
		if (*fmt != '%') {
			vga_putchar(*fmt++);
			continue;
		}
		fmt++; /* skip '%' */
		if (*fmt == '\0')
			break;
		if (*fmt == 'c') {
			/* char is promoted to int through varargs */
			vga_putchar((char)va_arg(ap, int));
		} else if (*fmt == 's') {
			const char *s = va_arg(ap, const char *);

			vga_puts(s ? s : "(null)");
		} else if (*fmt == 'd') {
			print_signed(va_arg(ap, int32_t));
		} else if (*fmt == 'u') {
			print_unsigned(va_arg(ap, uint32_t), 10);
		} else if (*fmt == 'x') {
			print_unsigned(va_arg(ap, uint32_t), 16);
		} else if (*fmt == '%') {
			vga_putchar('%');
		} else {
			vga_putchar('%');
			vga_putchar(*fmt);
		}
		fmt++;
	}
	va_end(ap);
}
