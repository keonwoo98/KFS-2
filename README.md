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

The container engine is auto-detected. A real `podman` wins over the `docker`
shim that Fedora ships, and podman automatically gets `:Z` (SELinux label) and
`--userns=keep-id`. Force one with `make CONTAINER=docker`.

`make run` prints how to quit before it boots. QEMU's curses display puts the
terminal in raw mode, so Ctrl-C is delivered to the guest instead of QEMU;
kill the container by name from a second terminal:

```sh
podman kill kfs2-run    # or: docker kill kfs2-run
reset                   # only if the terminal is left garbled
```

Rebuilding regenerates `kfs.iso`, so `git status` will show it modified;
restore the committed turn-in image with `git checkout kfs.iso`. Inside
`make shell`, run targets as `make all IN_CONTAINER=1`.

## 42 cluster (Fedora, no sudo)

The home quota is small, so clone into `/goinfre`. Nothing else is needed --
podman is detected automatically:

```sh
cd /goinfre/$USER && git clone <this repo> KFS-2 && cd KFS-2
make test
```

If podman still keeps its image storage in your home directory, relocate it
once with `sh scripts/cluster_setup.sh` (check `podman info --format
'{{.Store.GraphRoot}}'` first -- on most cluster accounts it already points at
/goinfre and the script is unnecessary).

Cluster hosts have a native QEMU, so the same ISO can be booted in a real
window instead of the terminal:

```sh
qemu-system-i386 -cdrom kfs.iso
```

## Layout

```
src/boot/boot.asm      multiboot v1 header, 16 KiB stack, jump to kernel_main
src/gdt/gdt.c          7-entry flat GDT built at 0x00000800
src/gdt/gdt_flush.asm  lgdt + segment register reload (far jump for CS)
src/kernel/kernel.c    kernel entry: magic check, gdt_init, banner, "42"
src/kernel/selftest.c  on-boot libk/scroll/GDT self-tests (print-on-failure)
src/kernel/stack_dump.c dump_hex + print_kernel_stack (esp..stack_top)
src/drivers/vga.c      VGA text driver (0xB8000): colors, scroll, hw cursor
src/lib/string.c       memset/memcpy/memmove/memcmp/strlen/strcmp
src/lib/printk.c       %c %s %d %u %x %p %% with zero-pad width (%08x, %02x)
src/drivers/keyboard.c polling PS/2 keyboard (scancode set 1, no IRQ)
src/shell/shell.c      debug shell: line editing, tokenizer, 7 commands
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

## Shell (bonus)

After the boot banners the kernel drops into a polling debug shell:

```
kfs> help
  help -- list commands
  stack -- hexdump the kernel stack
  gdt -- hexdump the GDT at 0x800
  dump -- dump <addr> [len=64, max 4096]
  clear -- clear the screen
  halt -- stop the CPU
  reboot -- reset via the 8042
```

`dump` takes `0x`-prefixed hex or plain decimal, so `dump 0x100000 32`
shows the first 32 bytes of the kernel image. Length is capped at one 4 KiB
page: with no IDT there is no way to interrupt a running command, so an
unbounded dump could never be stopped. There is no IDT yet, so the
keyboard is polled rather than interrupt-driven: `keyboard_poll()` returns
0 when the 8042 has nothing waiting. That costs a busy CPU while the shell
idles — `hlt` cannot be used, because with interrupts masked and no IDT it
would never wake.

`make test` boots without typing anything and checks the mandatory output.
`make test-shell` types a command through the QEMU monitor and checks what
the shell printed.
