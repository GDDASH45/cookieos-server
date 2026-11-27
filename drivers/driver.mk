# CookieOS Server 2026 - common ELF64 driver rules
# SPDX-License-Identifier: GPL-2.0-only

CC := gcc
LD := ld

DRIVER_NAME := $(notdir $(CURDIR))
SOURCES := $(shell find . -type f -name '*.c' -print | sort)
OBJECTS := $(SOURCES:.c=.o)
TARGET := $(DRIVER_NAME).sys

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror \
          -ffreestanding -fno-builtin -fno-stack-protector \
          -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -fno-unwind-tables -m64 -mno-red-zone -mno-mmx \
          -mno-sse -mno-sse2 -mcmodel=small \
          -I../include

.PHONY: all clean

all: $(TARGET)

%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	@echo "  LD -r   $@"
	@$(LD) -r -m elf_x86_64 $(OBJECTS) -o $@

clean:
	@rm -f $(OBJECTS) $(TARGET)
