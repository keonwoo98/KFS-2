#ifndef PRINTK_H
# define PRINTK_H

void printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
