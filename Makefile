# ===================== configuration ======================
NAME      := kernel.bin
ISO       := kfs.iso
IMAGE     := kfs2-build
CONTAINER ?= docker                # cluster: make CONTAINER=podman
PLATFORM  := linux/amd64           # BIOS GRUB (grub-pc-bin) only exists on x86 Debian

# ---- toolchain (used inside the container) ----
CC      := i686-linux-gnu-gcc
LD      := i686-linux-gnu-ld
ASM     := nasm

CFLAGS  := -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
           -nostdlib -nodefaultlibs -Wall -Wextra -Werror -O2 -g -Iinclude \
           -MMD -MP
ASFLAGS := -f elf32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

SRC_C   := $(shell find src -name '*.c' | sort)
SRC_ASM := $(shell find src -name '*.asm' | sort)
OBJ     := $(SRC_ASM:.asm=.o) $(SRC_C:.c=.o)
DEP     := $(OBJ:.o=.d)

ifndef IN_CONTAINER
# ===================== host layer ==========================
# Every real build/run happens inside the container image.

ifeq ($(CONTAINER),podman)
VOLFLAGS := -v "$(CURDIR)":/kfs:Z --userns=keep-id
else
VOLFLAGS := -v "$(CURDIR)":/kfs
endif
RUN := $(CONTAINER) run --rm -e BOOT_WAIT --platform $(PLATFORM) $(VOLFLAGS) -w /kfs

all: image
	$(RUN) $(IMAGE) make all IN_CONTAINER=1

run: image
	$(RUN) -it $(IMAGE) make run IN_CONTAINER=1

test: image
	$(RUN) $(IMAGE) make test IN_CONTAINER=1

shell: image
	$(RUN) -it $(IMAGE) bash

image:
	$(CONTAINER) build --platform $(PLATFORM) -t $(IMAGE) .

clean:
	find src \( -name '*.o' -o -name '*.d' \) -delete
	rm -rf isodir

fclean: clean
	rm -f $(NAME) $(ISO)

re: fclean all

.PHONY: all run test shell image clean fclean re

else
# ===================== container layer =====================

all: $(ISO)

-include $(DEP)

$(NAME): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) $(OBJ) -o $@
	grub-file --is-x86-multiboot $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) $(ASFLAGS) $< -o $@

$(ISO): $(NAME) grub.cfg
	mkdir -p isodir/boot/grub
	cp $(NAME) isodir/boot/$(NAME)
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ isodir

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -display curses

test: $(ISO)
	sh tests/boot_test.sh " 42 " "selftest ok" "2badb002" \
		"ptest [X|str|-42|-2147483648|4294967295|deadbeef|%]" \
		"SCRL29" "!SCRL00" "!Booting" "!PANIC"
	@sz=$$(stat -c %s $(ISO)); \
	if [ $$sz -le 10485760 ]; then \
		echo "OK   iso size: $$sz bytes (<= 10 MiB)"; \
	else \
		echo "FAIL iso too big: $$sz bytes"; exit 1; \
	fi

.PHONY: all run test

endif
