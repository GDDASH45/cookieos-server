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
OBJECTS := entry.o $(C_OBJECTS)

.PHONY: all clean

all: mtoskrnl.ckx

entry.o: entry.S
	@echo "  AS      $<"
	@$(CC) $(ASFLAGS) -c $< -o $@

src/%.o: src/%.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

mtoskrnl.ckx: $(OBJECTS) linker.ld
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) $(OBJECTS) -o $@

clean:
	@rm -f $(OBJECTS) mtoskrnl.ckx mtoskrnl.map