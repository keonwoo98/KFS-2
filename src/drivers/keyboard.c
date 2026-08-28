#include "keyboard.h"
#include "io.h"

#define KBD_DATA     0x60
#define KBD_STATUS   0x64
#define KBD_OBF      0x01   /* status bit 0: output buffer full */

#define SC_LSHIFT    0x2A
#define SC_RSHIFT    0x36
#define SC_EXTENDED  0xE0
#define SC_BREAK     0x80   /* release = make code | 0x80 */

/* US QWERTY, scancode set 1. Index = make code; 0 means "no character".
 * Two tables rather than a shift rule: shorter, and nothing to get wrong. */
static const char kbd_us[128] = {
	 0,    0,   '1',  '2',  '3',  '4',  '5',  '6',   /* 0x00-0x07 */
	'7',  '8',  '9',  '0',  '-',  '=', '\b', '\t',   /* 0x08-0x0F */
	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',   /* 0x10-0x17 */
	'o',  'p',  '[',  ']', '\n',   0,   'a',  's',   /* 0x18-0x1F */
	'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',   /* 0x20-0x27 */
	'\'', '`',   0,  '\\',  'z',  'x',  'c',  'v',   /* 0x28-0x2F */
	'b',  'n',  'm',  ',',  '.',  '/',   0,   '*',   /* 0x30-0x37 */
	 0,   ' ',   0,    0,    0,    0,    0,    0,    /* 0x38-0x3F */
};

static const char kbd_us_shift[128] = {
	 0,    0,   '!',  '@',  '#',  '$',  '%',  '^',   /* 0x00-0x07 */
	'&',  '*',  '(',  ')',  '_',  '+', '\b', '\t',   /* 0x08-0x0F */
	'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',   /* 0x10-0x17 */
	'O',  'P',  '{',  '}', '\n',   0,   'A',  'S',   /* 0x18-0x1F */
	'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',   /* 0x20-0x27 */
	'"',  '~',   0,   '|',  'Z',  'X',  'C',  'V',   /* 0x28-0x2F */
	'B',  'N',  'M',  '<',  '>',  '?',   0,   '*',   /* 0x30-0x37 */
	 0,   ' ',   0,    0,    0,    0,    0,    0,    /* 0x38-0x3F */
};

static bool g_shift;
static bool g_extended;

char kbd_translate(uint8_t sc, bool shift)
{
	if (sc >= 128)
		return 0;
	return shift ? kbd_us_shift[sc] : kbd_us[sc];
}

void keyboard_init(void)
{
	g_shift = false;
	g_extended = false;
	/* GRUB/BIOS may have read a key half-way and left a byte behind; without
	 * this the shell sees a phantom keystroke the moment it starts. Same
	 * reasoning as vga_set_cursor: do not inherit the bootloader's state. */
	while (inb(KBD_STATUS) & KBD_OBF)
		(void)inb(KBD_DATA);
}

char keyboard_poll(void)
{
	uint8_t sc;

	if (!(inb(KBD_STATUS) & KBD_OBF))
		return 0;                       /* nothing waiting: non-blocking */
	sc = inb(KBD_DATA);

	/* 0xE0 announces an extended key (arrows, etc). Its second byte arrives
	 * separately, possibly on a later poll, so the state has to be kept here.
	 * Dropping only the prefix would leak the second byte as a stray char. */
	if (g_extended) {
		g_extended = false;
		return 0;
	}
	if (sc == SC_EXTENDED) {
		g_extended = true;
		return 0;
	}
	if (sc & SC_BREAK) {                /* key release */
		sc = (uint8_t)(sc & 0x7F);
		if (sc == SC_LSHIFT || sc == SC_RSHIFT)
			g_shift = false;
		return 0;
	}
	if (sc == SC_LSHIFT || sc == SC_RSHIFT) {
		g_shift = true;
		return 0;
	}
	return kbd_translate(sc, g_shift);
}
