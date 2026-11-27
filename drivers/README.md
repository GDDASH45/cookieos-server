# CookieOS Server driver build system

All native drivers are built as x86-64 ELF relocatable objects with the
`.sys` extension. For example, the `hello` driver produces
`hello.sys`.

The files are ELF64 `ET_REL` objects, not standalone executables. They
contain sections, symbols, and relocation records for a future kernel
module loader to process.

## Driver ABI

A driver includes `<driver.h>`, defines the metadata record with
`COOKIEOS_DRIVER_METADATA(name, version)`, and exports:

- `int driver_init(void)`: return zero when initialization succeeds.
- `void driver_exit(void)`: release resources during unload.

The current ABI is version 1. Kernel service imports, dependency handling,
relocation processing, and runtime loading are not implemented yet.

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
