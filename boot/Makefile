ARCH := x86_64
CC := gcc
LD := ld
OBJCOPY := objcopy

EFI_INC := /usr/include/efi
EFI_LIB := /usr/lib
EFI_CRT0 := $(EFI_LIB)/crt0-efi-$(ARCH).o
EFI_LDS := $(EFI_LIB)/elf_$(ARCH)_efi.lds

CFLAGS := -I$(EFI_INC) -I$(EFI_INC)/$(ARCH) \
          -DEFI_FUNCTION_WRAPPER -fshort-wchar \
          -fpic -ffreestanding -fno-stack-protector \
          -fno-stack-check -mno-red-zone \
          -maccumulate-outgoing-args

LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) \
           -shared -Bsymbolic

EFI_LIBS := -L$(EFI_LIB) -lefi -lgnuefi
EFI_SECTIONS := -j .text -j .sdata -j .data \
                -j .dynamic -j .dynsym -j .rel \
                -j .rela -j .reloc

.PHONY: all clean

all: BOOTX64.EFI CookieBoot.efi

chainloader.o: chainloader.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

main.o: src/main.c include/elf_loader.h include/bootinfo.h
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -Iinclude -c $< -o $@

elf.o: src/elf.c include/elf_loader.h include/bootinfo.h
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -Iinclude -c $< -o $@

chainloader.so: chainloader.o
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) $(EFI_CRT0) $< $(EFI_LIBS) -o $@

CookieBoot.so: main.o elf.o
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) $(EFI_CRT0) main.o elf.o $(EFI_LIBS) -o $@

BOOTX64.EFI: chainloader.so
	@echo "  EFI     $@"
	@$(OBJCOPY) $(EFI_SECTIONS) --target=efi-app-$(ARCH) $< $@

CookieBoot.efi: CookieBoot.so
	@echo "  EFI     $@"
	@$(OBJCOPY) $(EFI_SECTIONS) --target=efi-app-$(ARCH) $< $@

clean:
	@rm -f chainloader.o main.o elf.o chainloader.so CookieBoot.so \
		BOOTX64.EFI CookieBoot.efi
