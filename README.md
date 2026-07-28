# My_Kernel

A hobby kernel written in C and C++, built on top of the [Limine](https://github.com/limine-bootloader/limine) bootloader. This project is a personal learning exercise in OS development, with the goal of learning more about low-level systems programming, memory management, paging, scheduling and more.

## Building & Running

The project uses `make` for building.

```bash
make all
```

This compiles the kernel and generates a bootable ISO image.

Other useful targets:

- `make run` — build the kernel/ISO and boot it in QEMU (if installed)
- `make all-hdd` — build a raw HDD/USB image instead of an ISO
- `make run-hdd` — build and run the HDD image in QEMU

### Dependencies

- GNU Make
- A Clang/LLVM (or GCC cross) toolchain capable of cross-compilation
- `xorriso` for building ISO images
- `sgdisk` and `mtools` for building HDD/USB images
- `qemu` (optional, for the `run` targets)

## Status

This is very much a work in progress, built for learning rather than use. Expect things to be incomplete, not fully bug free or rewritten without notice!

Implemented so far:
- core cpu setup (GDT, TSS, LAPIC)
- interrupt handler (IDT, IRQ stubs)
- PMM, VMM and bitmap heap
- custom MLPQ scheduler (check src/scheduling/README.md for more information on it)
- threads and processes 

Working on:
- userland
- file system
- elf loader

## Acknowledgements

This project wouldn't be possible without the resources and tools below:

- [**Limine**](https://github.com/limine-bootloader/limine) by Mintsuki and contributors, for the bootloader and the C/C++ bare-bones template this kernel was built from.
- [**Flanterm**](https://github.com/mintsuki/flanterm) by Mintsuki and contributors, for framebuffer terminal handling.
- [**OSDev Wiki**](https://wiki.osdev.org/), an invaluable, condensed reference for just about every topic i needed for OS development.
- **Operating Systems: Three Easy Pieces** (OSTEP) by Remzi and Andrea Arpaci-Dusseau, for clear, intuitive explanations of core OS concepts.
- **Modern Operating Systems** by Andrew S. Tanenbaum, for deep, foundational coverage of operating system theory and design.
