# KotlTOS 0.2

KotlTOS is a native, single-tasking DOS-like OS for Upsilon. It boots from tape0
(drive A:) or a qcow2 hard disk (drive C:), with IO.SYS, COMMAND.EXE, AUTOEXEC.BAT,
a modal editor, a native assembler/linker and persistent files. The filesystem,
shell, editor and tools execute guest assembly; the emulator supplies devices.

## Build, install and boot

From the `upsilon` directory:

```sh
make kotltos              # creates missing media, then starts the machine
# Equivalent invocation after building:
./build/upsilon -m 256 -t kotltos.tap -d harddisk.qcow2
```

At the initial tape-booted `A:\>` prompt:

```text
FORMAT C:
SYSCOPY C:
SHUTDOWN
```

Then boot the installed disk, with or without the tape:

```sh
./build/upsilon -m 256 -d harddisk.qcow2
```

Startup offers **two seconds to press A** and enter `OK>`. Lowercase a also
works on a live terminal. From OK, use `boot disk0`, `boot disk1`, etc., or
`boot tape0`. Up to four images attach in `-d` option order (disk0..disk3).
Automatic startup tries those numbered disks in order, then tape0. Other
queued input is retained for the OS and starts boot immediately. With piped
input, uppercase A is the escape key; lowercase command text remains usable.
`--no-autoboot` enters OK directly. The startup window uses a host-monotonic
platform timer, independent of guest instruction throughput.

FORMAT accepts `C`, `C:` or `(C)`. It erases C:'s filesystem and bootability,
leaving A: untouched. It is a quick format: old unused data is not securely
erased. SYSCOPY copies the boot drive's root system files to an already formatted
C:, preserving unrelated C: files. `SYSCOPY A: C:` explicitly selects A: as source.
It validates the source set, invalidates the old boot sector, copies files, then
publishes a checksummed boot sector last. Interrupted installs are not bootable.
It installs IO.SYS, COMMAND.EXE, AUTOEXEC.BAT, VI.EXE, ASM.EXE, LINK.EXE,
SYSCOPY.EXE and BOOT.SYS. Copy your own files separately, for example
`COPY A:\HELLO.ASM C:\HELLO.ASM`.

Firmware device numbers and KotlTOS drive letters are separate. A disk boot
maps the selected `diskN` to C:. A tape boot maps disk0 to C:. Other attached
disks are boot-selectable; they do not receive additional DOS drive letters in
this version. To install another image, attach it as the first `-d` while booted
from tape, then run FORMAT C: and SYSCOPY C:.

Switch current drives with `A:` or `C:`; `CD C:\DOCS` also switches. Explicit
paths work across drives. On switching drives without a path, the other drive's
root is selected; per-drive saved working directories are not implemented.

At least 16 MiB RAM is required. `make kotltos` uses a billion-step limit;
`--max-steps` can override the default. Ctrl-C stops the emulator and restores
the terminal; completed saves are already durable. SHUTDOWN or keyboard EOF
stops KotlTOS. Use an ANSI-compatible terminal for VI.

`make kotltos-tape` creates `kotltos.tap` only when absent; `make kotltos-disk`
creates a blank 128 MiB `harddisk.qcow2` only when absent. Existing media and the
legacy `dostape` are preserved. `make clean` removes build artifacts, retaining
user media. `make kotltos-image` rebuilds the fresh template
`build/kotltos-install.tap`; `make dos-image` and `make dos` are compatibility
aliases. Older user tapes are not silently upgraded or overwritten: use the new
KotlTOS installation reel to install the new system on C:.

## qcow2 disks

Create another image with `build/upsilon mkdisk filename.qcow2 --size 128` (MiB).
Existing filenames are refused. `-d` / `--disk` attaches a numbered disk (repeat up to four times), and
`--disk-readonly` forces a read-only mount. The C backend requires no QEMU at
runtime. It supports standalone, unencrypted, uncompressed qcow2 v2/v3 images,
512-byte-aligned virtual sizes up to 1 GiB, standard L2 tables, and 16-bit
refcounts. Backing chains, snapshots, compression, dirty/corrupt images, external
data and extended L2 features are rejected explicitly. Reads support cluster
sizes 512 bytes through 2 MiB. Successful writes normalize the container to
sparse qcow2 v3 with 64 KiB clusters, preserving virtual disk contents.

The implementation follows the [QEMU qcow2 format specification](https://www.qemu.org/docs/master/interop/qcow2.html).
QEMU-created v2/v3 images are covered by optional independent interoperability
tests and checked with `qemu-img check`. Each write rebuilds a sparse sibling
image and atomically replaces the old one; this favors correctness over large
image performance. Only nonzero clusters occupy data space. A sidecar writer
lock permits one emulator writer; a second emulator gets a read-only snapshot.
Do not run external image modification tools while the image is mounted.

Tape images use the same one-writer policy and `--tape-readonly` option. Lock
files `<image>.lock` may remain after exit; the process-held lock is released.

## Boot files

The ROM’s `UPS9BOOT` tape loader or C: boot sector loads the native
KotlTOS bootstrap at `0x80000`. The bootstrap replays the filesystem journal and
loads the boot volume’s latest `IO.SYS` into `0x100000`. IO.SYS installs the filesystem and
DOS syscall handlers, then loads the boot volume’s `COMMAND.EXE` at `0x180000`.

COMMAND.EXE executes AUTOEXEC.BAT from the boot volume before displaying
`A:\>` or `C:\>`. Those
are actual filesystem files, not names recognized and substituted by the host.
Deleting IO.SYS or COMMAND.EXE prevents the next boot. Updating AUTOEXEC.BAT
changes the next startup, for example:

```text
VI AUTOEXEC.BAT
```

VI.EXE, ASM.EXE, LINK.EXE and SYSCOPY.EXE are independently loaded native program launchers
for the corresponding native IO.SYS services. The assembler implementation is
the same guest `na_assemble` used by the ROM monitor, linked into IO.SYS. DOS
syscalls enter guest handlers through the architectural VBR; none are host calls.

## Commands

Commands and DOS paths ignore case. Tape0 is drive `A:` and the hard disk is `C:`. Paths accept `\` or `/`,
rooted paths such as `A:\DOCS\NOTE.TXT`, relative paths, `.` and `..`. Components
use 8.3 names containing letters, digits, `_` or `-`; paths are limited to 63
bytes. The root directory is implicit.

| Command | Action |
| --- | --- |
| `DIR [path/pattern]` | List direct children; `*` and `?` match names |
| `TYPE file` | Print the exact file bytes |
| `COPY source destination` | Copy a file, including its executable/object metadata |
| `DEL file/pattern`, `ERASE` | Delete matching files; directories are skipped |
| `REN old new`, `RENAME` | Rename within a volume; existing targets are refused |
| `MD path`, `MKDIR` | Create a directory; its parent must exist |
| `RD path`, `RMDIR` | Remove an empty directory, excluding the current directory |
| `CD [path]`, `CHDIR` | Change or display the current directory |
| `ECHO text` | Print text |
| `ECHO text > file` | Create or replace a text file |
| `ECHO text >> file` | Append a line to a text file |
| `VI file`, `EDIT file` | Open the native modal editor; create a file with `:w` |
| `ASM source.asm [output.obj]` | Assemble/validate a native object; default extension `.OBJ` |
| `LINK output.exe input.obj [...]` | Link up to eight objects into a runnable native executable |
| `CALL script.bat` | Execute a batch file; four nested batch levels |
| `FORMAT C:` | Erase and initialize C:; removes bootability |
| `SYSCOPY [A:] C:` | Install a bootable system on formatted C: |
| `A:`, `C:` | Switch the current drive |
| `VER`, `CLS`, `HELP` | Version, clear terminal, command help |
| `EXIT`, `SHUTDOWN` | Halt DOS |

Bare program names search for `.BAT`, then `.EXE`, in the current directory and
in the boot volume’s root. Explicit extensions are accepted. Batch files support these same
commands, blank lines, `REM`, `ECHO ON/OFF`, leading `@` to suppress echo, and
nested CALL. There is no variable/parameter expansion, IF/GOTO, environment,
piping or general redirection. Only ECHO implements `>`/`>>`. COPY takes explicit
names; directory renames and recursive copy/delete are not implemented.

Cross-drive COPY is supported; REN cannot cross drives.

Each successful modifying command persists immediately. A failed write leaves
the old file and the old tape image intact. Read-only commands do not append
records. `DIR *.*` includes extensionless names, as in DOS.

## VI editor

The editor has a 20-line ANSI viewport, a following cursor, normal/insert modes,
modified-state display, and an 8191-byte plain-text buffer.

| Keys | Action |
| --- | --- |
| `i`, `a` | Insert before / after the cursor |
| `I`, `A` | Insert at line start / end |
| `o` | Open a new line below |
| Escape | Return to normal mode |
| `h j k l`, arrow keys | Move left, down, up, right |
| `0`, `$` | Line start / end |
| `gg`, `G` | File start / end |
| `x`, `dd` | Delete a character / line |
| `u` | Undo the previous edit, one level |
| `:w [filename]` | Save, optionally under a new filename |
| `:q` | Quit, refusing unsaved changes |
| `:q!` | Quit and discard changes |
| `:wq`, `:x` | Save and quit |

Insert mode accepts printable ASCII, tabs and newlines; Backspace deletes the
preceding byte. EOF discards unsaved changes. Long display lines are clipped
to 78 columns; bytes beyond that remain in the file. Tabs display as one space.
There are no searches, visual selections, counts, registers or multiple buffers.

## Assemble, link and run

The installation includes HELLO.ASM and a two-module example:

```text
ASM HELLO.ASM
LINK HELLO.EXE HELLO.OBJ
HELLO

ASM MAIN.ASM
ASM GREET.ASM
LINK BOTH.EXE MAIN.OBJ GREET.OBJ
BOTH
```

The second program resolves `greet` from GREET.OBJ. Edit these files with VI.
Use `.extern name` for external references, `.globl name` for exports, and one
`.entry symbol` across the complete program. Symbol names are case-sensitive
and must be unique across modules; there are no private per-object namespaces.
The assembler accepts the ISA's integer/FP instructions and addressing forms.
Undefined symbols at final link, duplicate definitions and invalid instructions
report diagnostics and preserve any previous destination file.

UOBJ1 is a small native **source-IR object format**, not ELF or MS-DOS OMF. It
contains both the assembler's emitted machine bytes and the original assembly
IR. During validation, declared externals have placeholder values. The linker
combines the modules' IR, removes `.extern` declarations, resolves definitions,
and runs native branch/address relaxation to emit final machine code. This
allows short branches to change size after linking; the object preview bytes
are not the final executable. There are no libraries, relocation-only linking,
GOT/PLT, ELF sections, or x86 binary compatibility. `.text`/`.data`/`.bss` remain
flat markers and `.globl` does not restrict symbol visibility.

Objects allow up to 8191 source bytes and 8128 preview-code bytes. Final link
accepts up to eight modules, 8191 combined/expanded source bytes and 8128 emitted
machine bytes, with 256 symbols. Executables load at `0x200000`; entry addresses
are stored in filesystem metadata. Programs receive a NUL-terminated command
tail in R0 and normally return with RET. Faulting applications return an error
to COMMAND.EXE. This is a DOS-style shared address space, without process
isolation or multitasking.

A minimal program is:

```asm
.entry start
start:
    LEA message, R0
    MOV.q #0, R14          ; DOS puts
    SYSCALL #33
    CLR.q R0              ; success exit status
    RET
message: .asciz "Hello from KotlTOS!\n"
```

## Native DOS ABI

`SYSCALL #33` dispatches on R14. R0..R15 and F0..F2 are caller-clobbered;
R16..R29, FP and the calling stack are preserved. Flags are restored by RTE;
check the signed result in R0 rather than a carry flag. Negative results are
errors. Addresses are guest physical pointers in the shared address space.

| R14 | Inputs | Result |
| --- | --- | --- |
| 0 | R0=NUL-terminated string | Print; R0=0 |
| 1 | R0=buffer, R1=capacity including NUL | Line length; -1 EOF, -2 overflow |
| 2 | R0=directory/pattern or empty string | Print directory |
| 3 | R0=name, R1=buffer, R2=capacity | R0=length, R1=kind, R2=entry |
| 4 | R0=name, R1=data, R2=length, R3=kind, R4=entry | Persist file |
| 5 | R0=name, R1=0 file / 1 empty directory | Delete |
| 6 | R0=old name, R1=new name | Rename file |
| 7 | R0=VI command tail | Run editor |
| 8 | R0=ASM command tail | Assemble object |
| 9 | R0=LINK command tail | Link executable |
| 10 | R0=executable name, R1=command tail | Run application, return its status |
| 11 | R0=name | Make directory |
| 12 | R0=path | Change directory |
| 13 | R0=64-byte buffer | Copy current directory |
| 14 | none | Halt OS |
| 15 | R0=exit status | Terminate active application |
| 16 | R0=name | TYPE file |
| 17 | R0=byte | Print character |
| 18 | none | Print version |
| 19 | R0=drive string (C:) | Format C: |
| 20 | R0=SYSCOPY command tail | Install bootable system on C: |

Kinds are data=0, executable=1, object=2, directory=3. File capacity is 65536
bytes, with 64 file/directory slots per volume and 128 slots in the combined RAM cache. File errors -1..-13 correspond to
missing file, size limit, bad name, missing path, directory full, write protection,
tape write failure, disallowed operation, app fault, bad service, invalid object,
nesting limit, and assembly/link failure.

## Tape format and persistence

After the boot file's tape mark, each SIMH record is one filesystem event.
The emulator understands only record framing; guest filesystem code interprets
the following little-endian header:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | `UPSDOS1\0` |
| 8 | 8 | Sequence, starting at 1, increasing by one |
| 16 | 8 | Operation: put=1, delete=2, rename=3 |
| 24 | 64 | Canonical old/file path, NUL-terminated |
| 88 | 64 | New path for rename, NUL-terminated |
| 152 | 8 | Data length |
| 160 | 8 | Kind |
| 168 | 8 | Executable entry |
| 176 | 8 | FNV-1a-64 of header+data, this field zeroed |
| 184 | 8 | Reserved |
| 192 | length | File data; absent for delete/rename |

Mount replays events in order. On the first invalid event it retains previously
recovered files and mounts them read-only; it does not guess past corruption.
SIMH framing itself is validated by the emulator before guest boot. Deletions
free live directory slots but retain older records. The reel grows to the
controller's 512 MiB limit; this initial OS has no tape compactor.

Saving builds one complete event in RAM, sets the controller's physical DMA
address/length, and issues APPEND. The controller writes a temporary sibling
image, synchronizes it, and atomically renames it over the mounted image before
reporting success. It preserves the boot file and all previous records. Failed
CPU instructions cannot append; failed host writes leave the old image intact.
RAM directory changes happen only after a successful device response. This
persists across emulator restarts, not just within an emulated RAM session.

The expanded controller registers are documented in the implementation profile.
One APPEND record is committed per instruction, at EOM regardless of the read
cursor. The host installer can seed records with `mktape --add NAME=path`; the
running OS has no such host-file access.

UOBJ1 files have a 32-byte header: magic `UOBJ1\0\0\0`, source length, preview-code
length, and preview entry (all lengths/entry are quadwords). The original source
bytes follow, then preview machine bytes. The tape event supplies file kind and
final executable entry metadata separately.

## Disk filesystem and boot layout

C: uses the same checksummed file-event format as A:, with canonical `C:\`
paths. Its own sequence, read-only state and journal tail are independent of A:.
A failed C: operation cannot append to the tape. The combined RAM cache has
128 slots, enforcing a separate 64-entry quota per volume.

| Disk byte offset | Contents |
| --- | --- |
| 0 | 512-byte boot sector: `KOTLBOOT`, load address, entry, payload length, FNV-1a-64 of payload (quadwords at 8,16,24,32) |
| 4096 | BOOT.SYS bootstrap, at most 65536 bytes; loaded at 0x80000 |
| 0x20000 | 512-byte superblock: `KOTLVOL1`, version=1, committed journal tail, sequence, checksum (quadwords at 8,16,24,32) |
| 0x21000 | File-event journal, ending at the published tail |

The superblock checksum covers its first 40 bytes with the checksum field zero.
A save writes its complete event first, then durably publishes the new tail and
sequence. An interruption before publication leaves an ignored orphan; a later
save can overwrite it. Mount validates bounds, sequences, paths and checksums.
Corrupt C: metadata never causes writes to A:. Published boot payloads are
checksummed by firmware before execution. FORMAT atomically resets the disk's
boot/superblock region and clears only C: cache entries.

Numbered legacy block controllers start at 0xffff0300 + 0x40 * disk-number: STATUS byte +0 (online=1,
read-only=2, error=4, ready=8, auto-boot enabled=16), COMMAND byte +8 (read=1,
write=2, flush=3), disk byte offset quad +16, physical DMA buffer quad +24,
length quad +32, capacity quad +40 (read-only). DMA is synchronous at CPU commit,
limited to 1 MiB and valid physical RAM. Failed instructions cannot issue I/O;
read DMA cannot overwrite ROM. It is not a virtio-mmio device.
