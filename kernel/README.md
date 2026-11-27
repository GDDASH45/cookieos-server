# MT kernel

SPDX-License-Identifier: GPL-2.0-only

The initial MT kernel is a freestanding x86-64 ELF64 executable linked at
physical address 1 MiB. CookieBoot loads its PT_LOAD segments and jumps to
`_start`.

CookieBoot passes a pointer to `COOKIEOS_BOOT_INFO` in RDI. The assembly
entry point establishes a private 16 KiB stack and calls `kernel_main`
using the x86-64 System V ABI.

The first milestone initializes COM1 (115200 8N1), validates boot-information
ABI v1, and reports the memory map, ACPI RSDP, and GOP framebuffer metadata.
It then halts. Paging, memory management, interrupt handling, scheduling,
and device drivers are not implemented yet.

Build with `make -C kernel`. The resulting `kernel/mtoskrnl.ckx` is the
ELF64 kernel image to place at `\\cookieos\\System\\mtoskrnl.ckx`
on the boot volume.