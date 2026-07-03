# KFS-2 — GDT & Stack

Freestanding i386 kernel for 42's *Kernel From Scratch 2*: builds on the
KFS-1 base (GRUB multiboot boot, VGA text driver, libk, printk) and adds
a 7-entry flat GDT (null + kernel/user code/data/stack) installed at
physical `0x00000800` via `lgdt`, plus a human-friendly kernel stack
hexdump built on printk.

## Requirements

Any host with Docker **or** rootless Podman. Nothing else — compiler,
GRUB, QEMU all live in the build container (Debian, pinned to
`linux/amd64` because BIOS GRUB modules only exist on x86).

## Usage

| command | effect |
|---|---|
| `make` | build `kernel.bin` + `kfs.iso` in the container |
| `make run` | boot the ISO in QEMU (terminal/curses display) |
| `make test` | headless boot + assert screen contents + ISO size |
| `make shell` | interactive shell inside the build container |
| `make clean` / `fclean` / `re` | the usual |

With Podman: `make CONTAINER=podman <target>` (adds `:Z` volume label and
`--userns=keep-id` automatically).

To leave `make run`: press `ESC` then `2` to reach the QEMU monitor and
type `quit` (if the terminal ends up garbled, run `reset`).

Inside `make shell`, run targets as `make all IN_CONTAINER=1`.

## 42 cluster (Fedora, no sudo)

```sh
sh scripts/cluster_setup.sh        # once: podman storage -> /goinfre
make CONTAINER=podman test
```

## Layout

```
src/boot/boot.asm      multiboot v1 header, 16 KiB stack, jump to kernel_main
src/gdt/gdt.c          7-entry flat GDT built at 0x00000800
src/gdt/gdt_flush.asm  lgdt + segment register reload (far jump for CS)
src/kernel/kernel.c    kernel entry: magic check, gdt_init, banner, "42"
src/kernel/selftest.c  on-boot libk/scroll/GDT self-tests (print-on-failure)
src/kernel/stack_dump.c dump_hex + print_kernel_stack (esp..stack_top)
src/drivers/vga.c      VGA text driver (0xB8000)
src/lib/string.c       memset/memcpy/memmove/memcmp/strlen/strcmp
src/lib/printk.c       %c %s %d %u %x %% with zero-pad width (%08x, %02x)
include/               kernel headers (types.h is the freestanding base)
linker.ld              custom linker script (kernel at 1 MiB)
grub.cfg               GRUB menu entry
tests/boot_test.sh     QEMU-monitor VGA-dump assertions
```

## How the boot works

BIOS → GRUB finds the multiboot header in `kernel.bin` (inside `kfs.iso`),
loads it at 1 MiB, switches to 32-bit protected mode, jumps to `_start`
(eax = `0x2BADB002`, ebx = multiboot info). `boot.asm` sets up a 16 KiB
stack and calls `kernel_main(magic, mb_info_addr)`, which validates the
magic, then builds the kernel's own GDT.

The GDT cannot be placed at `0x00000800` by the linker (GRUB refuses to
load ELF segments below 1 MiB), so `gdt_init` writes the 7 descriptors
there at runtime, loads GDTR with `lgdt`, reloads DS/ES/FS/GS (`0x10`)
and SS (`0x18`), and far-jumps to reload CS (`0x08`). All segments are
flat (base 0, 4 GiB limit), so every pointer — including esp — stays
valid across the switch. Self-tests then read GDTR back with `sgdt` and
verify the live selectors, and `print_kernel_stack` hexdumps the region
esp..stack_top (the stack grows downward from `stack_top`).
