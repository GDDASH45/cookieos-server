ARCH := x86_64
CC := gcc
LD := ld
OBJCOPY := objcopy

EFI_INC := /usr/include/efi
EFI_LIB := /usr/lib
EFI_CRT0 := $(EFI_LIB)/crt0-efi-$(ARCH).o
EFI_LDS := $(EFI_LIB)/elf_$(ARCH)_efi.lds

CFLAGS := -I$(EFI_INC) -I$(EFI_INC)/$(ARCH) \
          -DEFI_FUNCTION_WRAPPER \
          -fpic -ffreestanding -fno-stack-protector \
          -fno-stack-check -mno-red-zone \
          -maccumulate-outgoing-args

LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS) \
           -shared -Bsymbolic

TARGET := CookieBoot
OBJECTS := main.o

.PHONY: all clean

all: $(TARGET).efi

main.o: src/main.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).so: $(OBJECTS)
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) $(EFI_CRT0) $(OBJECTS) \
		-L$(EFI_LIB) -lefi -lgnuefi -o $@

$(TARGET).efi: $(TARGET).so
	@echo "  EFI     $@"
	@$(OBJCOPY) -j .text -j .sdata -j .data \
		-j .dynamic -j .dynsym -j .rel \
		-j .rela -j .reloc \
		--target=efi-app-$(ARCH) $< $@

clean:
	@rm -f $(OBJECTS) $(TARGET).so $(TARGET).efi
