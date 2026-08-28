# ===================== configuration ======================
NAME      := kernel.bin
ISO       := kfs.iso
IMAGE     := kfs2-build

# Container engine, auto-detected so that a plain `make` works everywhere.
# Prefer a real podman: the 42 cluster's Fedora ships a `docker` command that is
# only a shim over podman, and podman needs its own rootless flags (:Z for
# SELinux, --userns=keep-id) that the docker path does not add. Override with
# `make CONTAINER=docker`.
ifndef CONTAINER
CONTAINER := $(shell command -v podman >/dev/null 2>&1 && echo podman || echo docker)
endif
# Silence Docker Desktop's "What's next:" advertising block after every run.
export DOCKER_CLI_HINTS := false
# BIOS GRUB (grub-pc-bin) only exists on x86 Debian
PLATFORM  := linux/amd64

# ---- toolchain (used inside the container) ----
CC      := i686-linux-gnu-gcc
LD      := i686-linux-gnu-ld
ASM     := nasm

CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
           -fno-asynchronous-unwind-tables \
           -nostdlib -nodefaultlibs -Wall -Wextra -Werror -O2 -g -Iinclude \
           -MMD -MP
ASFLAGS := -f elf32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

SRC_C   := $(shell find src -name '*.c' | sort)
SRC_ASM := $(shell find src -name '*.asm' | sort)
OBJ     := $(SRC_ASM:.asm=.o) $(SRC_C:.c=.o)
DEP     := $(OBJ:.o=.d)

.DEFAULT_GOAL := all

# A failed recipe must not leave a half-written target behind: make would
# consider it up to date and the next build would skip the check that failed.
.DELETE_ON_ERROR:

.PHONY: all run test shell image clean fclean re

# ---- housekeeping: same rules inside and outside the container ----
clean:
	find src \( -name '*.o' -o -name '*.d' \) -delete
	rm -rf isodir

fclean: clean
	rm -f $(NAME) $(ISO)

# `re: fclean all` would let `make -j` run the two branches concurrently.
re:
	$(MAKE) fclean
	$(MAKE) all

ifndef IN_CONTAINER
# ===================== host layer ==========================
# Every real build/run happens inside the container image.

ifeq ($(CONTAINER),podman)
VOLFLAGS := -v "$(CURDIR)":/kfs:Z --userns=keep-id
# podman files locally built images under localhost/. Referred to by the bare
# name, `podman run` treats it as a short name and stops to ask which registry
# to pull it from - even though `podman build` just produced it.
IMAGE    := localhost/$(IMAGE)
else
VOLFLAGS := -v "$(CURDIR)":/kfs
endif
# --pull=never: the image is always the one the `image` target just built here,
# so neither engine should ever reach out to a registry for it.
RUN := $(CONTAINER) run --rm --pull=never -e BOOT_WAIT --platform $(PLATFORM) $(VOLFLAGS) -w /kfs

all: image
	$(RUN) $(IMAGE) make all IN_CONTAINER=1

# Named so the container can be killed by name: QEMU's curses display puts the
# terminal in raw mode, so Ctrl-C and the Esc-2 monitor hotkey are unreliable.
run: image
	@printf '\n  The kernel halts after printing; QEMU does not exit on its own,\n'
	@printf '  and its curses display swallows Ctrl-C. To quit, from a 2nd terminal:\n\n'
	@printf '      $(CONTAINER) kill kfs2-run\n      reset\n\n'
	@if [ -t 0 ]; then printf '  Press Enter to boot... '; read _; fi
	-@$(CONTAINER) rm -f kfs2-run >/dev/null 2>&1
	@$(RUN) -it --name kfs2-run $(IMAGE) make run IN_CONTAINER=1; \
	  st=$$?; case $$st in 0|130|137) ;; *) exit $$st ;; esac

test: image
	$(RUN) $(IMAGE) make test IN_CONTAINER=1

shell: image
	$(RUN) -it $(IMAGE) bash

image:
	$(CONTAINER) build --platform $(PLATFORM) -t $(IMAGE) .

else
# ===================== container layer =====================

all: $(ISO)

-include $(DEP)

# Makefile is a prerequisite of every compile/link: changing CFLAGS/ASFLAGS/
# LDFLAGS must rebuild, or the next link mixes objects built with old flags.
$(NAME): $(OBJ) linker.ld Makefile
	$(LD) $(LDFLAGS) $(OBJ) -o $@
	grub-file --is-x86-multiboot $@

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm Makefile
	$(ASM) $(ASFLAGS) $< -o $@

$(ISO): $(NAME) grub.cfg
	mkdir -p isodir/boot/grub
	cp $(NAME) isodir/boot/$(NAME)
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ isodir

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -display curses

test: $(ISO)
	sh tests/boot_test.sh " 42 " "selftest ok" "gdt ok" "2badb002" \
	    "ptest [X|str|-42|-2147483648|4294967295|deadbeef|0x00001234|%|00c0ffee|0f]" \
	    "kernel stack:" "02 b0 ad 2b" \
	    "SCRL29" "!SCRL00" "!Booting" "!PANIC"
	@sz=$$(stat -c %s $(ISO)); \
	if [ $$sz -le 10485760 ]; then \
	    echo "OK   iso size: $$sz bytes (<= 10 MiB)"; \
	else \
	    echo "FAIL iso too big: $$sz bytes"; exit 1; \
	fi

endif
