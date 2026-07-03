# Platform is pinned to linux/amd64 by the Makefile (build/run --platform):
# BIOS GRUB modules (grub-pc-bin) only exist on x86 Debian.
FROM debian:bookworm-slim

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

WORKDIR /kfs
