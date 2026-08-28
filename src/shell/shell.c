#include "shell.h"
#include "libk.h"

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
