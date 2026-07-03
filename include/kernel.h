#ifndef KERNEL_H
# define KERNEL_H

# include "types.h"

# define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

void kernel_main(uint32_t magic, uint32_t mb_info_addr);
int  selftest_run(void);
void dump_hex(const void *addr, uint32_t len);
void print_kernel_stack(void);

#endif
