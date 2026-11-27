# CookieOS Server 2026 external Ring 3 applications
# SPDX-License-Identifier: GPL-2.0-only
CC := gcc
LD := ld
CFLAGS := -std=c11 -O2 -Wall -Wextra -ffreestanding -fno-builtin \
          -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -m64 -mno-red-zone -mno-sse -mno-sse2 -mcmodel=small \
          -Iinclude

ASM_SOURCES := $(wildcard *.S)
C_SOURCES := $(wildcard *.c)
ASM_OBJECTS := $(ASM_SOURCES:.S=.o)
C_OBJECTS := $(C_SOURCES:.c=.o)
APP_OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)
APP_TARGETS := $(APP_OBJECTS:.o=.app)

.PHONY: all clean
all: $(APP_TARGETS)

%.o: %.S
	@echo "  AS      $<"
	@$(CC) -m64 -c $< -o $@

%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

%.app: %.o linker.ld
	@echo "  LD      $@"
	@$(LD) -nostdlib -static -z max-page-size=0x1000 -T linker.ld $< -o $@

clean:
	@rm -f $(APP_OBJECTS) $(APP_TARGETS)
