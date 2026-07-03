#include "libk.h"

void *memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;

	while (n--)
		*d++ = (uint8_t)c;
	return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t       *d = dst;
	const uint8_t *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	uint8_t       *d = dst;
	const uint8_t *s = src;

	if ((uintptr_t)d < (uintptr_t)s) {
		while (n--)
			*d++ = *s++;
	} else {
		while (n--)
			d[n] = s[n];
	}
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *pa = a;
	const uint8_t *pb = b;

	while (n--) {
		if (*pa != *pb)
			return (int)*pa - (int)*pb;
		pa++;
		pb++;
	}
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (int)(uint8_t)*a - (int)(uint8_t)*b;
}
