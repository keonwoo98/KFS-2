#ifndef SHELL_H
# define SHELL_H

# include "types.h"

void shell_run(void);   /* does not return */

/* Exposed (not static) so the on-boot selftest can exercise them without
 * producing any screen output. See the design spec, section 10. */
int shell_parse_u32(const char *s, uint32_t *out);   /* 0 ok, -1 malformed */
int shell_tokenize(char *line, char **argv, int max); /* returns argc */

#endif
