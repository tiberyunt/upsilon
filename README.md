# Upsilon

Native development tools and a C11 simulator for the Upsilon 64-bit CISC ISA.
The current architecture is **v0.4**.

| Directory | Contents |
| --- | --- |
| [docs/](docs/README.md) | Current ISA, programmer's guide and implementation profile |
| [assembler/](assembler/README.md) | Independent native assembler, UOBJ1 linker and ROM utilities |
| [simulator/](simulator/README.md) | CPU, MMU, serial console, ELF/FDT boot, tape and qcow2 devices |
| common/ | Shared ISA tables and types; no simulator dependency |
| examples/ | Assembly examples and native monitor/BASIC/assembler firmware |
| [dos/](dos/README.md) | KotlTOS source, editor, native tools and installer inputs |
| tests/ | C/Python regression tests; the Python implementation is a test oracle in tests/reference/ |

## Build

Requires a C11 compiler with 128-bit integer support, Make and Python 3 for tests.
No GNU cross-compiler or NetBSD source download is required for the main build.

```sh
make                         # all three native executables
make assembler linker        # tools only; does not build or execute the simulator
make simulator               # simulator with generated monitor ROM
make test
```

The executables are `build/upsilon-as`, `build/upsilon-ld` and `build/upsilon`.
`make -C assembler` builds only the tools. `make -C simulator` builds the
simulator. `make install PREFIX=/your/prefix` installs all three.

```sh
build/upsilon-as examples/fibonacci.s -o build/fibonacci.bin
build/upsilon run build/fibonacci.bin --binary
build/upsilon-as -c module.s -o build/module.obj
build/upsilon-ld build/module.obj other.s -o build/program.bin --map build/program.json
```

The native linker combines source IR and relaxes the final instruction stream;
it is not an ELF linker. See [the assembler notes](assembler/README.md).
For compatibility, `upsilon asm` delegates to `upsilon-as`; it does not supply
the assembler implementation. Direct `upsilon run source.s` remains available.

## Firmware and KotlTOS

```sh
make hello                   # serial hello ROM
make basic-demo              # BASIC demo input
make firmware                # native OK monitor (BASIC, editor, assembler)
make kotltos                 # create missing media and boot KotlTOS
build/upsilon -m 256 -t exampletape -d harddisk.qcow2
```

Existing tape and disk files are preserved. New installation images are built
under `build/`. Press A during startup to enter the OK monitor; device boot
uses `boot tape0` or `boot disk0`. KotlTOS drive C: is the first hard disk.
See [KotlTOS documentation](dos/README.md) for FORMAT, SYSCOPY and its tools.

## Separate ports

The GNU ports and NetBSD port live in separate sibling repositories:

- [upsilon-gnu](../upsilon-gnu/README.md): GCC/Binutils overlays, reproducible builds and runtime.
- [upsilon-netbsd](../upsilon-netbsd/README.md): NetBSD MD sources and bootstrap probe.

They are not vendored or submodules in this repository. Optional convenience
commands are `make gnu-test` and `make netbsd-test`. Override `GNU_DIR` and
`NETBSD_DIR` if the sibling repositories have different locations. Their
scripts also accept `UPSILON_ROOT` and `UPSILON_GNU_ROOT` as documented there.
The NetBSD port is still a bootstrap probe, not a complete kernel/userland.

## Repository hygiene

Git excludes build output, compiled objects/executables, tape reels, disk
images, locks and caches. Keep local saved media outside version control.
Only current documentation is included; older drafts are not part of this repo.

## Licensing

The software is licensed under the [GNU GPL v3](LICENSE).
Except where otherwise noted, original documents in `docs/` are licensed under
[CC BY-NC 4.0](docs/LICENSE.md).
