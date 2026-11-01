# Upsilon simulator

C11 implementation of the Upsilon CPU, MMU, traps, floating point, serial I/O,
SIMH nine-track tape transport and native qcow2 disks.

```sh
make -C .. simulator
../build/upsilon --help
../build/upsilon -m 256 -t reel.tap -d disk.qcow2
../build/upsilon -m 32 --kernel kernel.elf --bootargs '-s'
```

The default ROM is assembled from source by the independent native assembler
at build time. Source execution is also supported (`upsilon run program.s`),
using the shared assembler library. Compatibility commands `upsilon asm`,
`upsilon link` (legacy ROM composition) and `upsilon embed` delegate to a sibling
`upsilon-as` executable; tools can be installed together with `make install`.

Read the [implementation profile](../docs/reference-profile.md) for supported
instructions, the ELF/FDT boot interface and device-register contracts.
