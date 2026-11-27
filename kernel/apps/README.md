# CookieOS external applications

Applications are standalone ELF64 x86-64 executables linked for the user virtual
address range beginning at `0x40000000`. They run at CPL 3 and use the
`int 0x80` syscall interface.

Build every top-level `.S` and `.c` application in this directory:

```sh
make -C kernel/apps
```

The matching linker script is `linker.ld`. Do not link against a hosted C
runtime or libc. A C application must provide its own `_start` entry point.

The syscall header is `include/cookieos/syscall.h`:

- `cookieos_write(text, length)` writes a user buffer to the kernel console.
- `cookieos_exit(status)` terminates the application.

To install the startup application into the FAT EFI System Partition:

```sh
mcopy -o -i qemu/esp.img kernel/apps/autoexec.app ::/cookieos/System/autoexec.app
```

The MT kernel reads that file itself after boot services have exited. CookieBoot
does not preload the application.
