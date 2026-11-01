KotlTOS 0.2
Tape0 is A:, qcow2 hard disk is C:. File changes persist on each volume.
Press A at startup to enter OK; boot disk0 (or disk1 etc.) or boot tape0 manually there.
FORMAT C: initializes C:; SYSCOPY C: installs a bootable system on it.

DIR [path/pattern]              TYPE filename
COPY source dest        DEL filename        REN old new
MD directory            CD directory        RD empty-directory
ECHO text > file        ECHO text >> file    CALL script.bat
VI/EDIT filename        ASM file.asm [file.obj]
LINK output.exe first.obj [second.obj ...]
HELP                    VER                 CLS             SHUTDOWN

VI is a modal ANSI terminal editor: i/a/o insert, Esc returns to normal.
h/j/k/l move, 0/$ line ends, gg/G file ends, x deletes, dd deletes a line,
u undoes one edit. :w saves, :q exits, :q! discards, :wq saves and exits.

Try:
 ASM HELLO.ASM
 LINK HELLO.EXE HELLO.OBJ
 HELLO
 ASM MAIN.ASM
 ASM GREET.ASM
 LINK BOTH.EXE MAIN.OBJ GREET.OBJ
 BOTH

Everything executes as native Upsilon instructions. No host assembler service.
Objects contain assembled bytes plus source IR; the linker resolves symbols
and relaxes branches across modules. Use .extern name for external symbols,
.globl name for exports, and only one .entry across the linked program.
Executables run at 0x200000 and return with RET (or syscall function 15).

Limits: 64 files/directories per volume, 65536 bytes/file, 8.3 components, 63-byte paths.
DIR and DEL accept * and ?; COPY uses explicit file names. REN is for files.
Editor/source text: 8191 bytes; linking: 8 objects, 8128 machine-code bytes.
No x86/MS-DOS binary compatibility, networking or multitasking.
AUTOEXEC.BAT on the boot volume is run by COMMAND.EXE on every boot. Use VI to change it.
The tape journal grows with saves/deletes; capacity is 512 MiB. No compactor yet.
Use the same tape image on the next run. A second writer mounts read-only.
