# Platform is pinned to linux/amd64 by the Makefile (build/run --platform):
# BIOS GRUB modules (grub-pc-bin) only exist on x86 Debian.
# Fully qualified: podman has no implicit docker.io, and an unqualified name
# either prompts interactively or fails with a short-name resolution error.
FROM docker.io/library/debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    make \
    nasm \
    gcc-i686-linux-gnu \
    binutils-i686-linux-gnu \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    qemu-system-x86 \
    gawk \
    && rm -rf /var/lib/apt/lists/*

# QEMU's curses backend converts CP437 glyphs to the locale charset; the slim
# image defaults to ASCII, so every glyph >= 0x80 fails and floods the terminal.
# C.UTF-8 is built into glibc 2.36 (bookworm) - no extra package needed.
ENV LANG=C.UTF-8

WORKDIR /kfs
