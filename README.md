# KFS-1 — Grub, boot and screen

Freestanding i386 kernel for 42's *Kernel From Scratch 1*: boots via GRUB
(multiboot v1), drives the VGA text buffer (colors, newline, scroll,
hardware cursor), ships a small libk and a `printk`, and prints `42`.

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

Rebuilding regenerates `kfs.iso`, so `git status` will show it modified;
restore the committed turn-in image with `git checkout kfs.iso`. Inside
`make shell`, run targets as `make all IN_CONTAINER=1`.

## 42 cluster (Fedora, no sudo)

```sh
sh scripts/cluster_setup.sh        # once: podman storage -> /goinfre
make CONTAINER=podman test
```

## Layout

```
src/boot/boot.asm      multiboot v1 header, stack, jump to kernel_main
src/kernel/kernel.c    kernel entry: magic check, banner, "42"
src/kernel/selftest.c  on-boot libk/scroll self-tests (print-on-failure)
src/drivers/vga.c      VGA text driver (0xB8000)
src/lib/string.c       memset/memcpy/memmove/memcmp/strlen/strcmp
src/lib/printk.c       %c %s %d %u %x %%
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
magic, runs self-tests and prints to the VGA text buffer.
