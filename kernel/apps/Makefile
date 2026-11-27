# CookieOS Server 2026 external Ring 3 applications
# SPDX-License-Identifier: GPL-2.0-only
CC := gcc
LD := ld
CFLAGS := -m64 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mcmodel=small -Iinclude
.PHONY: all clean
all: autoexec.app
 autoexec.o: autoexec.S
	@echo "  AS      $<"
	@$(CC) $(CFLAGS) -c $< -o $@
autoexec.app: autoexec.o linker.ld
	@echo "  LD      $@"
	@$(LD) -nostdlib -static -z max-page-size=0x1000 -T linker.ld $< -o $@
clean:
	@rm -f autoexec.o autoexec.app
