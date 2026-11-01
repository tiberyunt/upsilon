# Kotl Upsilon implementation profile v0.4

The production C tools and native ROM assembler follow the supplied [v0.4
specification](0.4.md), with section 25 taking precedence, and the
[assembly programmer’s guide](Kotl%20Upsilon%20Assembly%20Programmer%E2%80%99s%20Guide.md).
The source documents are retained unchanged. Reassemble firmware and application
binaries: narrow register writes and trap frames have incompatible semantics.
This is a functional single-CPU workstation core with the existing UART/tape
platform. The native [KotlTOS](../dos/README.md) is implemented; the
NetBSD workstation platform/port remains future work.

## Implemented instruction coverage

* All defined primary integer instructions and sizes, three-operand operations,
  branches, short instructions, and addressing modes. LEA is quad only.
* F0: every mandatory Appendix B operation, binary16/32/64. MFFS/MTFS have
  type bits zero. Optional binary128 (`.x`) is rejected, with no ID feature bit.
* F1: MOVS, FILL, CMPS, SCAN, SCANN at each element size.
* F2: EXTV, EXTZV, INSV, CMPV; short/long BBS, BBC, BBSS, BBCC, BBSSI, BBCCI.
  These are unsized, with quad operands and plain second-byte operation numbers.
* F3: CAS, XADD, SWAP, ATAND, ATOR, ATXOR, with natural alignment checks;
  CAS2 compares/swaps 16 bytes using even integer register pairs. It has no
  size variants: unsuffixed `CAS2` and `.q` emit F3 1B. Other size codes fault.
  This fixes the draft’s unspecified size bits for op 6; “Fn style” in A.3
  describes pairing, not the floating register file.
* F4: MOVZ, MOVSX, CVTIF, CVTUF, CVTFI, CVTFU, CVTFIR, CVTFF, MOVIF, MOVFI.
  Both widths are required: `CVTIF.q.d R0,F0` or `CVTIF.qd R0,F0`. `ITOF` and `FTOI` remain
  source aliases for CVTIF and CVTFIR; they emit v0.2 bytes.
* F5: CLZ, CTZ, POPCNT, BSWAP, BREV, MULW, UMULW, DIVW, UDIVW.
  Multiplication has four operands; division uses `divisor,Rn` and the Rn/Rn+1
  pair. Every narrow integer or FP register write zero-extends; memory writes
  affect only their operand bytes, including MOVSX to a narrow destination.
* F6: MFSR, MTSR, TLBI, DCLEAN, DINVAL, DFLUSH, HALT (F6 06), IOFENCE,
  UAON, UAOFF.
* F7: short/long SOB, SOBGE, SOBGT, AOBLT, AOBLE, ACB; CASE with an immediate
  limit and following signed 32-bit offsets relative to table start.

* EE: MFTP, readable in every mode, with flags unchanged. Long addressing
  kind 3 is signed disp32(TP), including indexed bases. TP autoincrement and
  TP memory-indirect forms are rejected.
* F8: SETcc, CMOVcc at all integer widths; PAUSE, PREFETCH, PREFETCHW.
  Condition 14 is spelled SETAL/CMOVAL; condition 15 is reserved. CMOV always
  reads its source. A false condition resolves the destination (including
  register autoupdates) but does not access destination memory. Literal
  destinations fault even on false conditions. Flags remain unchanged.
  Hints perform no address evaluation, autoupdates, data accesses or TF traps;
  fetching/decoding their encoding still uses normal instruction rules.

The assembler selects short/long branch forms, including F2/F7, and the
shortest representable immediate/displacement. Reserved encodings fault.
Integer size codes are 0/1/2/3; FP type codes are **0/1/2** for `.h/.s/.d`.
F4 combines integer-size and FP-type codes as specified, not uniform byte sizes.
`MOVIF`/`MOVFI` require equal source/destination widths and copy raw bits.

## Execution rules and remaining draft choices

Operands are evaluated left to right against a working register file. Sources
are read before the next specifier is evaluated. Explicit destination writes
follow operand evaluation; in `MOV.q (R1)+,R1`, the destination write wins.
This follows section 4.4; section 8's “original register values” means rollback
on fault. Instructions stage register changes, RAM writes, MMIO events, and
page-table A/D updates, and commit only after all checks succeed.

SUB3 and DIV3 compute second source minus/divided by first. Integer MUL/DIV
are signed; DIV truncates toward zero, faults on zero, and sets V on signed
quotient overflow while delivering the truncated result. MULW/DIVW are signed,
UMULW/UDIVW unsigned. Wide divide faults on an unrepresentable quotient and
preserves flags. MIN/MAX compare signed values and set N/Z, clear V, preserve C.
ABS sets V for the minimum signed value. XCHG/SWAP preserve flags. Atomic logical
operations preserve flags; XADD uses ADD flags and CAS changes only Z.

Shift counts are unsigned and saturate beyond the operand width; SAR sign-fills.
C is the last shifted bit, or zero beyond the width for SHL/SHR, or the sign for
SAR. Rotates reduce the count modulo width. Zero counts preserve C. All clear V.
Bit utilities/extracts set N/Z, clear V, preserve C; INSV and bit branches
preserve flags. Register bit positions are reduced modulo 64; fields crossing
bit 63 after that reduction fault (cross-register wrapping is unspecified).
Atomic bit forms access the aligned containing quad and update one bit within
it. SOB variants require an integer register. AOB/ACB use a final CMP for flags.

ENTER/PUSHM/POPM use ordinary quad operands for 16-bit masks; invalid masks
fault. EXIT derives caller SP from the frame and restores old FP even if FP/SP
appear in the mask. POPM with SP in its mask explicitly restores that saved SP.
JMP/JSR/LEA/cache ranges require memory address modes: `JMP (R0)` jumps to R0.
MFSR/MTSR require literal/immediate system IDs. TLBI kind 0/1 requires `#0` as
its address; kind 2/3 uses a memory address mode. Caches/fences/TLBI are functional
no-ops on the single-CPU coherent model, with privilege and operand checks.

## Block progress

Each step handles one element, and PC remains at the instruction while more
work remains. Completed elements survive faults and interrupts. Forward MOVS
advances pointers; overlapping destination-above-source copies work backward.
Both finish with R0=0 and R1/R2 past their respective final elements, as v0.2
requires. The draft does not specify how backward progress and final end
pointers are represented simultaneously in only R0–R2. The simulator retains a
transactional continuation record keyed by instruction address for the original
end pointers, with remaining count/base pointers in R0–R2. CPU snapshots include
this record. External context serialization of only registers is insufficient
for an in-progress backward MOVS; an OS-level continuation format remains open.
Ranges must not wrap or overlap solely via distinct virtual aliases.

CMPS stops at a mismatch without consuming it and compares destination minus
source. SCAN/SCANN leave R1 at the match and R0 including it; exhaustion leaves
R0=0 and Z clear. Empty CMPS sets Z. Fill/search explicit operands are evaluated
again for each element; use stable registers/immediates for repeated values.

## Floating point

FPSR uses v0.2: sticky flags **bits 0..4** (inexact, underflow, overflow,
divide-by-zero, invalid), trap enables **8..12**, rounding mode **16..17**.
All reserved bits read zero; reset is zero. Exception traps roll back every
register, including FPSR, with the triggering flag mask in FAULTINFO. This
resolves Appendix B's competing statements that sticky flags are set and that
an enabled exception changes no register in favor of full restartability.

Finite add/subtract/multiply/divide/remainder/fused arithmetic use exact rational
intermediates and one correctly directed IEEE rounding. Square root uses integer
arithmetic to round correctly. All four FPSR rounding modes, subnormals, signed
zero, overflow saturation, and after-rounding tininess are supported. The four
fused instructions round only once. NaNs are canonicalized; payload propagation
is not promised. FCLASS and comparisons distinguish signaling NaNs using raw
operand bits. FCLASS itself does not raise invalid. FCMP raises invalid for an
sNaN, FCMPE for any NaN. Only FCMP/FCMPE/FTST change integer flags in F0.

CVTFI/CVTFU truncate; CVTFIR uses FPSR rounding. Out-of-range conversion saturates
and signals invalid; NaN becomes zero. Integer-result conversions have the
specified N/Z/V behavior and preserve C. Host decimal source literals are parsed with C strtod to
binary64; imm32 is selected when exact, otherwise imm64, independently of
opcode format. Short constants follow B.4 (including negative zero at 23),
rounded to operand format nearest-even. Transcendental table entries use their
binary64 constants before narrowing. Binary128 is not implemented.

## System and memory

System IDs follow v0.4. Added CPUID=5 (zero), TIMEFREQ=6, TP=7, ESP=8,
TIMECMP=28, MCSTATUS/MCADDR/MCCTL=29..31, MPUCTL=40, PRBASE0..15=64..79,
PRLIM0..15=80..95, PRATTR0..15=96..111, WPADDR0..3=112..115 and
WPCTL0..3=116..119. Reserved system numbers raise illegal instruction (1),
before operand access/privilege checks. ID/CYCLES/CPUID/TIMEFREQ are read-only.
IDs 0..6 allow server/kernel access; the rest require kernel. MFTP permits
user TP reads. Reserved PSR/FPSR bits are masked, including PSR FPD/UA/TF.
MC registers read zero because no ECC extension is implemented.

ID advertises revision 1, profile W, MMU, MPU and binary FP, PA52/VA48 and a
nominal 64-byte cache-line granule. No binary128, hypervisor, decimal, crypto,
vector, ECC or multiprocessor feature is advertised. The core has no cache
latency model. The expanded v0.4 C implementation is authoritative; Python
modules remain an independent reference for the earlier instruction subset,
with zero-extension, ID and trap-frame expectations updated for differential
checks. They do not implement the new v0.4 system/debug facilities.

TIMEFREQ is 1,000,000 Hz; CYCLES advances one tick per committed execution
step (one element for blocks). PERFCNT counts those steps when PERFCTL bit 0
is set. TIMECMP defaults to UINT64_MAX (disabled); other mutable system
registers default to zero. Timer vector 64 is level 6 and remains pending while
CYCLES >= TIMECMP; a new compare takes effect at the next instruction boundary.
WAIT fast-forwards deterministic virtual time to an eligible timer. It remains
suspended when the timer is disabled or masked. This is not wall-clock timing.
`cpu_runnable` exposes whether the host should call `cpu_step` again.

Traps select the kernel stack and push PC then old PSR: `[SP]` is PSR and
`[SP+8]` PC. Faults save the instruction start; SYSCALL/BREAK save the successor.
Trap entry clears UA/TF, sets PMODE, records CAUSE/FAULTADDR/FAULTINFO, and reads
the VBR handler. RTE pops PSR then PC and restores the appropriate stack bank.
FPD raises vector 15 before FP operand evaluation (including MFFS/MTFS).
FAULTINFO memory bits are read=1, write=2, execute=4. SYSCALL records its operand.

TF delivers vector 17 only after a whole instruction completes; its committed
register, RAM and I/O effects survive even if trace delivery fails. Restoring
TF with RTE takes effect on the next instruction. Watchpoints use the v0.4
WPCTL layout and match overlapping virtual accesses before commit; enabling a
misaligned address raises reserved operand. Instruction decoding fetches bytes
individually, so a fetch hit identifies the byte that overlapped the watchpoint.

A delivery failure takes double fault (19) on ESP. NMI (20) and fatal machine
check (14) also use ESP. ESP must name a valid writable kernel stack; failure
of emergency delivery stops with vector 19 and no partial frame. NMI ignores
IPL; further NMIs remain queued until RTE from the emergency frame. Internal
emergency-frame state restores the interrupted kernel stack on RTE.
`cpu_interrupt(c,20,7)` injects NMI; maskable injection accepts only timer
64/level 6, IPI 65/level 5, and external 66/levels 1..7. Equal-priority queued
requests use arrival order. SIMASK/server routing is not modeled.

The MMU supports canonical 48-bit addresses, PTBR0/PTBR1, 4 KiB/2 MiB/1 GiB
leaves, transactional A/D bits, and page/access faults. Fetch PRIV must equal
MODE. Data requires PRIV >= MODE, and kernel/server access to less-privileged
pages requires UA. PTE MT bits 52..53 are honored; type 3 and bits 54..63 are
reserved. Device memory disallows fetch and unaligned data access. All bytes
are checked before any device read; FP operands are read once. Memory accesses
are sequential, so IOFENCE needs no additional functional action.

With translation off, MPUCTL bit 0 enables 16 physical regions; the lowest
matching base <= address < limit wins. As a provisional choice, PRATTR uses
PTE-compatible R/W/X bits 1/2/3 and PRIV bits 4..5, with MT bits 8..9. Empty
regions are inactive. No matching region permits kernel and faults in other
modes. The same execute/UA checks apply to matched regions. Reserved attribute
bits are ignored. This layout should be revisited if a later draft fixes it.

There is no TLB; ASID/global bits have no cache effect. Physical accesses outside
RAM report access violation without translation, bus error with translation.
MMUCTL affects the next fetch.

The optional serial port, ROM boot map, and keyboard EOF convention are platform
choices, described in the README. MMIO receive/transmit effects commit in order
with the instruction. ROM guest writes fault. There is no hardware timing,
cache, multiprocessor model, interrupt controller, vector extension, host
syscall ABI, or a complete workstation device set. A polled NS16550 register bank and
ELF/FDT boot now support the NetBSD-source bootstrap probe. KIC, virtio-mmio
and RTC/power registers remain unimplemented; the ROM/tape boot ABI is unchanged.
The full NetBSD kernel and syscall/signal ABI remain future port work.
The optional GNU port now provides static ELF linking/relocations; see below. Native
KotlTOS supplies its own guest syscall ABI, files, editor and source-IR
assembler/linker independently of that roadmap.

## Native monitor assembler ABI

The old simulator service is removed. The monitor now calls `JSR na_assemble`,
and the complete parse/encode/relax process executes Upsilon instructions.
No SYSCALL is intercepted. Both assemblers accept `.bss` as a flat section
marker and `.globl` as symbol metadata with no emitted bytes. This is not an
ELF linker. `*disp(Rn)` aliases `[disp(Rn)]`. The production host emulator and toolchain are C;
Python modules remain as independent test references.

Inputs retain R0=source pointer, R1=source length (0..8191), R2=output pointer,
R3=capacity (1..8128), R4=diagnostic pointer, R5=diagnostic capacity, R6=origin.
Success returns R0=0, R1=length, R2=entry. A source error returns R0=1 and
R1=R2=0 with a bounded NUL-terminated line diagnostic. R16..R31 and FPSR are
preserved. R3..R15, integer condition flags and F0..F2 are caller-clobbered.
This is a guest function using physical RAM, not an atomic host operation;
callers must provide valid writable buffers as the monitor does.

Output is staged at 0x70000 and copied to the RAM file only on success. The
symbol table occupies 0x72000..0x77000 (256 records of 80 bytes); parser state
starts at 0x78000, its token/name/diagnostic scratch at 0x78400, the source-line
buffer at 0x79000, and decimal conversion big integers at 0x7c000/0x7c100.
Symbols are limited to 63 characters, source lines to 1023 bytes, decimal
significands to 128 digits. Decimal FP literals are converted nearest-even
with integer arithmetic; native floating compound expressions are not supported.

The RAM directory remains at 0x50000: eight 0x4000-byte slots. Each has filename[32]
at +0, source length (quad) +32, binary length +40, entry +48, NUL-terminated
source +64 (8192 bytes including NUL), binary +0x2040 (8128 bytes). A zero
first filename byte marks a free slot. Reset invalidates all slots. Editor
scratch at 0x42000 is separate from committed files. Command/name/diagnostic
buffers are 0x40000/0x40100/0x40200. BASIC retains its buffers and program area
below 0x40000. BOOT now supports the tape0 transport; other device names remain diagnostic placeholders.

The C emulator uses the same architectural rules above. Its write journal is
bounded at 4096 distinct bytes per instruction (more than any implemented
instruction needs), and it supports 16 simultaneously suspended backward MOVS
continuations. Host interrupts are queued through `cpu_interrupt`. The public
`cpu_step` rolls back faults within an instruction. Post-completion TF traps
retain completed effects, as described above.


## Reference-platform 9-track reel transport

The C emulator always maps tape controller registers at physical 0xffff0100,
returning offline status when no reel is mounted. Images are writable SIMH
.tap files; record lengths/trailers/padding, marks, EOM and full erase-gap
markers are validated while mounting. Error-flagged records remain readable
but set the record-error bit. The ninth parity track is represented by record
error metadata, not a physical signal decoder. There is no tape timing or IRQ.

| Offset | Width | Register |
|---|---|---|
| 0 | byte | DATA: consumes one byte of the selected record; zero if exhausted |
| 4 | byte | STATUS: online=1, data ready=2, file mark=4, EOM=8, error=16, BOT=32, auto-boot request=64, write-protected=128 |
| 8 | byte | COMMAND: write 1=rewind, 2=read next record, 3=space forward, 4=backspace, 5=APPEND; reads return last command |
| 16 | quad | Selected record length, little-endian, read-only |
| 24 | quad | Position of the next tape object (records and marks), read-only |
| 32 | quad | Bytes remaining in the selected record, read-only |
| 40 | quad | Physical DMA source address, read/write |
| 48 | quad | DMA length in bytes, read/write |

Other offsets are reserved. DATA/STATUS/COMMAND require byte accesses; the
three counters and two DMA registers permit naturally aligned byte/word/long/quad
accesses within their bounds. DMA writes and commands are staged with the CPU
instruction. APPEND takes 1..1048576 bytes from physical RAM, includes earlier
staged RAM writes from the same instruction, appends one SIMH record before EOM,
and preserves all prior records and tape marks. It ignores the read cursor.
On success the cursor is after the new record; its data is exhausted. Invalid
DMA, write protection, allocation failure or host I/O failure sets ERROR without
a CPU fault or a partial appended record. A further command in the same
instruction cancels a pending APPEND and sets ERROR.
READ selects a whole record and positions after it, so another READ discards
any unread bytes. SPACE similarly advances but discards the buffer. BACKSPACE
positions before the preceding object and clears the selected buffer. REWIND
clears marks/errors and the auto-boot request. Unsupported commands set error.
The controller is visible through ordinary MMU mappings and leaf permissions.
Register accesses cannot be used for instruction fetch. The controller's cursor,
record selection and flags use a shadow state per CPU instruction and commit
only on success; failed instructions cannot consume bytes, move the reel or
append to the host file. Durable append uses a temporary sibling image, file
fsync, atomic rename and best-effort parent-directory fsync. The image mode is
preserved. An advisory sidecar lock `<image>.lock` permits one writer; other
processes mount read-only snapshots. `--tape-readonly` forces read-only and
releases the writer lock. A filesystem that does not support atomic rename or
writer locking must be treated as unsupported for concurrent writable use.

A read-only quad at physical 0xffff0200 reports configured RAM bytes. This is
platform configuration, not an ISA system-register number. The native ROM
loader uses it to check load/entry/stack bounds and select a default stack.

The boot file is Upsilon-defined: a 48-byte UPS9BOOT version-1 header record
(magic[8], version/load/entry/length/stack quadwords), followed by payload
records terminated by mark or EOM. The firmware validates the entire first
file before loading, then repeats the read to copy payload through the device.
It permits load >=0x80000, entry inside the payload, and aligned SP between
payload end and RAM top. Zero SP uses RAM top. The source implementation is
examples/tape_boot.inc; the host emulator has no header-interception callback.

## KotlTOS block disks and startup

The C backend supports standalone, unencrypted, uncompressed qcow2 v2/v3 with
standard L2 tables and 16-bit refcounts, through 1 GiB virtual size. Dirty/corrupt
images, backing files, snapshots, compression, extended L2 and external data are
rejected. Writes atomically rebuild sparse qcow2 v3 with 64 KiB clusters;
`qemu-img check` and external v2/v3 conversion round trips verify metadata.
`-d` attaches images in disk0..disk3 order (up to four); `mkdisk path --size MiB` creates a new one without
replacing existing files; `--disk-readonly` disallows writes. Runtime QEMU is
not required. A sidecar writer lock and atomic replacement preserve the existing
image if a save fails. See the [KotlTOS guide](../dos/README.md) for filesystem
and boot structures; the emulator itself interprets only qcow2 block storage.

Four numbered legacy disk controllers at 0xffff0300 + 0x40*n have byte status +0 and command +8,
quad offset +16, DMA source/destination +24, length +32 and read-only capacity
+40. Read/write/flush commands are 1/2/3. Status bits are online=1, read-only=2,
error=4, ready=8, autoboot=16. Transfers commit only after instruction validation,
are bounded to 1 MiB of physical RAM, and cannot DMA-read into ROM. Earlier
staged RAM stores are visible to disk write DMA. I/O failures report device
ERROR, not a partially committed CPU instruction. Flush is a synchronous no-op
because every write has already been persisted. This does not implement virtio.

The UART adds read-only byte PEEK at +5. Platform 0xffff0208 provides a
monotonic millisecond timer; byte 0xffff0210 reports an interactive keyboard.
The native firmware uses these to offer a two-second A-key interruption before
trying checksummed disk0..disk3 bootstraps, then the existing tape loader. Other queued
input is preserved. Boot transfers R0=65 for A: or 67 for C:, and R1=selected disk number, to the
stage-0 loader. Manual firmware syntax is `boot diskN`, not a DOS drive letter;
the selected boot disk appears as C: inside KotlTOS. Tape boot selects disk0. The ISA CYCLES/TIMEFREQ virtual clock is unchanged.

## GNU cross-toolchain

The separate [GNU target port](../../upsilon-gnu/README.md) adds GCC 14.2.0, GAS,
BFD, GNU ld and objdump for `upsilon-unknown-elf`. It uses a provisional ELF64
RELA format with local experimental machine ID 0xf5a0 and a documented LP64
soft-float C ABI variant. It does not change the ISA or the native UOBJ1 object
format. The host tools can build bare-metal programs and KotlTOS executables;
they do not run inside the guest OS. See the port guide for coverage and limits.

## Workstation bootstrap platform

`--kernel image.elf` or `run image.elf --elf` loads validated ELF64 Upsilon
ET_EXEC segments, zeroes BSS and supplies an FDT in R0. The top 64 KiB of RAM
are reserved for the FDT; see the [boot contract](../../upsilon-netbsd/README.md).

A byte-wide polled NS16550-compatible console is mapped at 0xffff0400. It
shares the serial input/output connection with the legacy UART. Divisor
latches, line/modem control, RX/TX status, FIFO reset and scratch registers
are modeled transactionally. UART interrupts are not connected, so the FDT
advertises no interrupt property. KIC and virtio are still absent.

The [NetBSD bootstrap probe](../../upsilon-netbsd/README.md) compiles original NetBSD
11.0 standalone libraries and machine headers, enables page tables, handles
a recoverable page fault and timer interrupts, and tests user transitions and
BSD carry/errno syscall returns. It does not run the MI kernel or userland.

The MD context layer now compiles against actual NetBSD kernel LWP/CPU headers.
It tests native `cpu_switchto`, target-generated assembly offsets, per-context
TP/root/ASID/IPL state, timer delivery across switches, private user mappings,
and fault-contained copyin/out with kernel-stack recovery. The harness uses
cooperative contexts; MI scheduling remains unfinished.

An initial pmap core now implements address-space lifecycle/activation, managed
page reverse mappings, protection/removal, wired counts, reference/dirty
tracking and unmanaged kernel maps. Guest tests exercise actual MMU faults,
isolation, table-path cloning, and rollback/reclamation after pool exhaustion.
It uses fixed bootstrap pools and a managed-page provider; UVM allocation,
locking and demand paging remain unfinished.

The NetBSD MD layer now includes native CAS, add/logical/exchange operations
and FENCE barriers, with generic NetBSD routines supplying new-value and
inc/dec APIs. Guest tests cover GCC builtin ABI entry points, pointer aliases
and timer/mainline shared updates. This is UP groundwork; full C11 runtime
coverage and MI mutex/scheduler integration remain unfinished.

MD fork/exec context hooks now construct child uareas/trapframes, call the MI
startup ABI, initialize exec registers/TP, and return natively to user mode.
Probe tests execute fork-style and exec-style user programs plus a kernel
thread on their own stacks. The MI scheduler, fork/exec services and process
exit remain unintegrated; the probe supplies startup and exit harnesses.
