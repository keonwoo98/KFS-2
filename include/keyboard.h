#ifndef KEYBOARD_H
# define KEYBOARD_H

# include "types.h"

/* Polling PS/2 keyboard. No IDT, no PIC: the shell asks for a key, it does
 * not get told about one. See the design spec for why IRQ1 is out of scope. */
void keyboard_init(void);   /* drain the 8042 output buffer, reset state */
char keyboard_poll(void);   /* 0 if no key is ready, else one ASCII char */

/* Pure scancode -> ASCII, no hardware access. Exposed so the on-boot
 * selftest can check the translation table without pressing keys. */
char kbd_translate(uint8_t sc, bool shift);

#endif
