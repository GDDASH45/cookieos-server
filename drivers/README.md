# CookieOS Server driver build system

Native drivers are x86-64 ELF relocatable objects with the `.sys` extension.
For example, the `hello` driver produces `hello.sys`.

## Installing drivers

Copy driver files to this directory on the EFI System Partition:

```text
\cookieos\System\Drivers\
```

CookieBoot scans that directory, preloads up to 16 `.sys` files, and passes
their memory locations to the kernel in boot-information ABI version 2.
The kernel validates each ELF64 `ET_REL` image, lays out its allocatable
sections, applies supported x86-64 RELA relocations, checks the driver
metadata, and calls `driver_init()`.

The current loader supports local symbols and these relocations:
`R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_PLT32`,
`R_X86_64_32`, and `R_X86_64_32S`.

Undefined imports are rejected. Kernel service exports, dependencies,
unloading, and memory reclamation are not implemented yet. Driver code is
placed in a dedicated executable module pool; page-level permission
hardening will be added with the kernel's own memory manager.

## Driver ABI

Include `<driver.h>`, define metadata with
`COOKIEOS_DRIVER_METADATA(name, version)`, and export:

- `int driver_init(void)`: return zero when initialization succeeds.
- `void driver_exit(void)`: release resources during unload.

The current ABI version is 1. Each driver is linked independently, so
each may use the same entry-point symbol names.

## Building

From the repository root:

```sh
make drivers
```

To clean driver build artifacts:

```sh
make clean
```

Each immediate subdirectory containing C sources is treated as one driver.
All C sources below that directory are compiled and combined with
`ld -r` into one ELF64 `.sys` file.
