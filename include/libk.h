#ifndef LIBK_H
# define LIBK_H

# include "types.h"

/* Freestanding gcc may emit calls to memset/memcpy/memmove/memcmp on its
 * own (struct copies, loop idioms), so the kernel must provide exactly
 * these standard symbols. */
void   *memset(void *dst, int c, size_t n);
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);
size_t  strlen(const char *s);
int     strcmp(const char *a, const char *b);

#endif
