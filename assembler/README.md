# Native assembler and linker

`upsilon-as` and `upsilon-ld` are independent host binaries. They link only the
assembler, source loader and shared ISA tables; neither links CPU, device or
simulator code, invokes the simulator, or requires a firmware image.

```sh
make -C .. assembler linker
../build/upsilon-as input.s -o output.bin --origin 0x1000 --map symbols.json
../build/upsilon-as -c main.s -o main.obj
../build/upsilon-as -c helpers.s -o helpers.obj
../build/upsilon-ld main.obj helpers.obj -o program.bin --origin 0x200000
```

The assembler supports includes, expressions, labels, data directives and
instruction relaxation. `-c` emits native **UOBJ1** source-IR objects: the
32-byte little-endian header contains magic, source length, preview length and
preview entry; source bytes and preview machine bytes follow. Declared externs
have placeholder values only in the preview. The final linker combines source
IR, removes `.extern` declarations and reassembles/relaxes against actual
symbol definitions. It can also consume plain `.s` modules. Undefined symbols,
duplicate definitions, malformed objects and multiple entry declarations fail
before writing the output binary. Symbols share one namespace across modules.
`.globl` is accepted but does not create private symbol visibility.

The format is compatible with the source-IR layout used by KotlTOS; guest tools
have stricter input-size limits. It is not ELF, and there are no archives,
dynamic linking or relocation-only links. The optional GNU ELF tools belong to
the separate GNU port repository.

The ROM-specific source composer remains available as
`upsilon-as rom-link monitor.s basic.s native.s -o firmware.s`; this renames
BASIC symbols and connects monitor return paths. It is distinct from the
ordinary UOBJ1 linker. `upsilon-as embed image.bin -o firmware_image.h`
generates the simulator's built-in ROM header.
