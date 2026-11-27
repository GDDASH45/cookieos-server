# CookieOS Server 2026 - MT kernel
# SPDX-License-Identifier: GPL-2.0-only

CC := gcc
LD := ld

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-stack-protector \
          -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -fno-unwind-tables -m64 -mno-red-zone -mno-mmx \
          -mno-sse -mno-sse2 -mcmodel=small \
          -Iinclude -I../boot/include -I../drivers/include

ASFLAGS := -m64
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 \
           -T linker.ld -Map=mtoskrnl.map

C_SOURCES := $(shell find src -type f -name '*.c' -print)
C_OBJECTS := $(C_SOURCES:.c=.o)
OBJECTS := entry.o arch/x86_64/ring3.o arch/x86_64/demo_app.o $(C_OBJECTS)

.PHONY: all clean

all: mtoskrnl.ckx recovery.ckx

entry.o: entry.S
	@echo "  AS      $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

arch/x86_64/%.o: arch/x86_64/%.S
	@echo "  AS      $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

src/%.o: src/%.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

recovery/entry.o: recovery/entry.S
	@echo "  AS      $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

recovery/main.o: recovery/main.c include/framebuffer.h ../boot/include/bootinfo.h
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

RECOVERY_LDFLAGS := -nostdlib -static -z max-page-size=0x1000 \
                    -T recovery/linker.ld -Map=recovery.map

recovery.ckx: recovery/entry.o recovery/main.o src/framebuffer.o recovery/linker.ld
	@echo "  LD      $@"
	@$(LD) $(RECOVERY_LDFLAGS) recovery/entry.o recovery/main.o src/framebuffer.o -o $@

mtoskrnl.ckx: $(OBJECTS) linker.ld
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) $(OBJECTS) -o $@

clean:
	@rm -f $(OBJECTS) mtoskrnl.ckx mtoskrnl.map recovery.ckx recovery.map \
		recovery/entry.o recovery/main.o