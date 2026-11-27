# MT kernel

SPDX-License-Identifier: GPL-2.0-only

The MT kernel is a freestanding x86-64 ELF64 executable linked at physical
address 1 MiB. CookieBoot loads its PT_LOAD segments and jumps to `_start`.

CookieBoot passes a pointer to `COOKIEOS_BOOT_INFO` in RDI. The current
boot-information ABI is version 2. The assembly entry point establishes a
private 16 KiB stack and calls `kernel_main` using the x86-64 System V ABI.

The kernel initializes the GOP framebuffer console, clears it to black, and
prints boot information. CookieBoot preloads up to 16 `.sys` files from
`\\cookieos\\System\\Drivers` on the EFI System Partition. The kernel's
internal ELF64 `ET_REL` loader places allocatable sections in its module
pool, applies supported x86-64 RELA relocations, validates the driver ABI,
and invokes `driver_init()`.

The current driver loader supports local relocations only. Undefined kernel
imports, dependency management, driver unloading, and memory reclamation
are not implemented. The module pool is currently writable and executable;
page-level permission hardening depends on the kernel memory manager.

Paging, general memory management, interrupt handling, and scheduling are
not implemented yet.

Build the full project with `make`, or build the kernel with
`make -C kernel`. The resulting `kernel/mtoskrnl.ckx` is the ELF64 kernel
image to place at `\\cookieos\\System\\mtoskrnl.ckx` on the boot volume.
