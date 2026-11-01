# Kotl Upsilon — Draft Specification v0.4

Working draft. Everything marked **(provisional)** is a proposed default to be confirmed or changed before the assembler and simulator are written.

## 1. Goals and conventions

**Goals.** An elegant, orthogonal, 64-bit CISC architecture in the tradition of the VAX-11 and the 68000, designed to be (1) easy to program and (2) compact in code size.

**Design rule.** Make the common case short and keep the rules regular. Any instruction accepts any addressing mode for any operand, and the size suffix and flag behaviour are uniform.

**Conventions.**

- Little-endian. Multi-byte immediates, displacements and addresses in the instruction stream store the low byte first.
- Data sizes: byte (`.b`, 8 bits), word (`.w`, 16), long (`.l`, 32), quad (`.q`, 64).
- Unaligned data access is allowed for all ordinary loads and stores. Atomic operations require natural alignment.
- Instructions are byte-aligned and variable length.
- Assembler syntax is source-first: `ADD.l src, dst`.
- Bits are numbered from 0 (least significant).

## 2. Programmer model

### 2.1 Registers

| Name | Count | Width | Notes |
| --- | --- | --- | --- |
| R0–R31 | 32 | 64 bits | General purpose. R31 is SP, R30 is FP. |
| F0–F31 | 32 | 64 bits | Floating point. |
| PC | 1 | 64 bits | Not in the register file; reached only through addressing modes and branches. |
| PSR | 1 | 64 bits | Flags, interrupt priority and privilege mode. |
| FPSR | 1 | 64 bits | Floating-point rounding mode, exception flags, trap enables. |

Sub-register access follows the operand size. A write of a size smaller than 64 bits **zero-extends** the result into the whole destination register (section 25.1). The same holds for floating-point registers.

### 2.2 Register conventions (software, not hardware)

| Registers | Role |
| --- | --- |
| R0–R7 | Arguments and return values. Caller-saved. |
| R8–R15 | Temporaries. Caller-saved. |
| R16–R29 | Callee-saved (14 registers). |
| R30 | Frame pointer. |
| R31 | Stack pointer. |
| F0–F31 | All caller-saved. |

Extra arguments go on the stack. The caller removes them after the call, so variadic calls need nothing special.

### 2.3 Processor status register (provisional layout)

| Bits | Field | Meaning |
| --- | --- | --- |
| 0 | C | Carry on add, borrow on subtract |
| 1 | V | Signed overflow (unordered after a floating-point compare) |
| 2 | Z | Zero |
| 3 | N | Negative |
| 8–10 | IPL | Interrupt priority level, 0–7 |
| 12–13 | MODE | 0 kernel, 1 server, 2 user, 3 hypervisor (profile S) |
| 14–15 | PMODE | Previous mode, saved on trap entry |
| 16 | FPD | Floating point disabled (section 18) |
| 17 | UA | User access: lets kernel or server code touch less-privileged pages (section 25.3) |
| 18 | TF | Trace flag: single-step trap after each instruction (section 25.4) |

All other bits are reserved and read as zero.

## 3. Instruction format

An instruction is an **opcode** (one byte, or a group byte plus a second opcode byte) followed by zero to four **operand specifiers**, in source-first order. Specifiers may be followed by extra bytes (displacements, immediates). Branch displacements follow the specifiers.

The length of an instruction is found by walking its specifiers in order; the first byte of each specifier says how many extra bytes follow. Implementations are expected to pre-decode lengths. Limits: at most 4 specifiers per instruction, so the maximum length is 48 bytes. Anything longer is an illegal instruction.

**Size field.** In the primary integer groups the low two bits of the opcode give the operand size: `00` byte, `01` word, `10` long, `11` quad.

## 4. Operand specifiers

Every specifier is one byte: **3 bits of mode (bits 7–5) and a 5-bit field (bits 4–0)**, usually a register number.

| Mode | Name | Syntax | Extra bytes |
| --- | --- | --- | --- |
| 0 | Register | `Rn` | none |
| 1 | Register deferred | `(Rn)` | none |
| 2 | Autoincrement | `(Rn)+` | none |
| 3 | Autodecrement | `-(Rn)` | none |
| 4 | Byte displacement | `d(Rn)` | 1 (signed) |
| 5 | Word displacement | `d(Rn)` | 2 (signed) |
| 6 | Index prefix | `base[Rx]` | the base specifier follows |
| 7 | Literal / escape | `#n`, escapes | see below |

### 4.1 Rules for the common modes

- Autoincrement and autodecrement step by the operand size (for the specifier’s own operand). Autodecrement decrements before the access, autoincrement increments after it.
- Displacements are sign-extended and little-endian.
- Register-direct (mode 0) names an F register when the operand type is floating point, otherwise an R register. The opcode determines the type.
- Effective addresses are 64-bit and computed with 64-bit arithmetic.

### 4.2 Index prefix (mode 6)

The prefix byte is `110` followed by Rx. It is followed by a complete base specifier. The effective address is the base specifier’s address plus Rx times the operand size. The base must be a memory mode (deferred, either displacement, or an absolute, PC-relative or long-form mode). Using a register, literal, immediate, autoincrement or autodecrement as the base is a **reserved addressing mode fault**. An index prefix may not be followed by another index prefix.

### 4.3 Literal and escape (mode 7)

The 5-bit field selects:

| Field | Meaning | Extra bytes |
| --- | --- | --- |
| 0–23 | Short literal, unsigned, zero-extended to the operand size | none |
| 24 | `imm8`, sign-extended to the operand size | 1 |
| 25 | `imm16`, sign-extended | 2 |
| 26 | `imm32`, sign-extended | 4 |
| 27 | `imm64` | 8 |
| 28 | `abs32`, absolute address, sign-extended | 4 |
| 29 | `abs64`, absolute address | 8 |
| 30 | PC-relative, signed 32-bit displacement from the end of the instruction | 4 |
| 31 | Long form: the next byte is `kind` (3 bits) plus `Rn` (5 bits) | 1 + data |

The immediate size is independent of the operand size, so a 64-bit operation with the constant 1000 uses `imm16`. The assembler always picks the shortest form.

**Floating-point literals.** For a floating-point operand, short literals 0–23 index a constant table (0.0, 1.0, 2.0, 0.5, 10.0 and so on; full table in Appendix B). `imm32` is a binary32 value and `imm64` a binary64 value.

**Long form kinds (provisional).** 0 is `disp32(Rn)`, 1 is `disp64(Rn)`, 2 is memory-indirect `[disp32(Rn)]` (load a 64-bit pointer from that address and use it as the effective address), and 3 is `disp32(TP)`: the thread pointer plus a 32-bit displacement, with the register field unused. Kinds 4–7 are reserved.

### 4.4 Evaluation rules

- Specifiers are evaluated **left to right**. Each specifier sees register changes made by the ones before it (so `ADD (R3)+, (R3)+, R4` reads two consecutive elements).
- Register changes from autoincrement and autodecrement commit only when the instruction can no longer fault (section 8).
- A literal or immediate used as a destination, or an address mode applied to a register operand that needs a memory address (for example `LEA` of a register), is a **reserved operand fault**.

## 5. Opcode map (provisional)

| Range | Count | Use |
| --- | --- | --- |
| `00`–`7F` | 128 | Up to two-operand integer ops: 32 ops, each in 4 sizes |
| `80`–`BF` | 64 | Three-operand integer ops: 16 ops, each in 4 sizes |
| `C0`–`DF` | 32 | Branches: 16 conditions, each with 8-bit and 32-bit displacement |
| `E0`–`EF` | 16 | One-byte instructions and short forms |
| `F0`–`FB` | 12 | Group bytes, each followed by a second opcode byte |
| `FC`–`FF` | 4 | Escape pages for future extensions |

### 5.1 Two-operand group (`00`–`7F`)

Opcode = `4 * op + size`. Operations (ops with one operand take one specifier):

| op | Name | op | Name |
| --- | --- | --- | --- |
| 0 | MOV src, dst | 16 | ROR src, dst |
| 1 | ADD src, dst | 17 | NEG src, dst |
| 2 | SUB src, dst | 18 | NOT src, dst |
| 3 | CMP src1, src2 | 19 | TST src |
| 4 | AND src, dst | 20 | LEA addr, dst (quad) |
| 5 | OR src, dst | 21 | PUSH src |
| 6 | XOR src, dst | 22 | POP dst |
| 7 | BIC src, dst (dst &= \~src) | 23 | XCHG a, b |
| 8 | MUL src, dst | 24 | INC dst |
| 9 | DIV src, dst | 25 | DEC dst |
| 10 | ADC src, dst | 26 | CLR dst |
| 11 | SBB src, dst | 27 | ABS src, dst |
| 12 | SHL count, dst | 28 | MIN src, dst |
| 13 | SHR count, dst (logical) | 29 | MAX src, dst |
| 14 | SAR count, dst | 30–31 | Reserved |
| 15 | ROL count, dst |  |  |

### 5.2 Three-operand group (`80`–`BF`)

Opcode = `0x80 + 4 * op + size`, with the destination last: `ADD3 a, b, dst`. Ops 0–15: ADD, SUB, MUL, DIV, AND, OR, XOR, BIC, SHL, SHR, SAR, ROL, ROR, ADC, SBB, reserved.

### 5.3 Branches (`C0`–`DF`)

Opcode = `0xC0 + 2 * cond + form`, where form 0 has an 8-bit displacement and form 1 a 32-bit displacement. Displacements are signed, little-endian, and relative to the end of the instruction.

| cond | Name | Test |
| --- | --- | --- |
| 0 | EQ | Z |
| 1 | NE | not Z |
| 2 | LT | N != V |
| 3 | LE | Z or N != V |
| 4 | GT | not Z and N == V |
| 5 | GE | N == V |
| 6 | LO | C |
| 7 | LS | C or Z |
| 8 | HI | not C and not Z |
| 9 | HS | not C |
| 10 | MI | N |
| 11 | PL | not N |
| 12 | VS | V |
| 13 | VC | not V |
| 14 | BRA | always |
| 15 | BSR | always, pushes the return address |

### 5.4 One-byte group (`E0`–`EF`) (provisional)

`E0` NOP, `E1` RET, `E2` EXIT, `E3` RTE (privileged), `E4` IFENCE, `E5` FENCE, `E6` BREAK, `E7` WAIT (privileged), `E8` JMP dst, `E9` JSR dst, `EA` ENTER mask, locals, `EB` PUSHM mask, `EC` POPM mask, `ED` SYSCALL #n, `EE` MFTP dst, `EF` reserved.

### 5.5 Group bytes (`F0`–`FB`) (provisional)

| Byte | Group |
| --- | --- |
| `F0` | Floating point: arithmetic, compare, FPSR access (`MFFS`, `MTFS`) |
| `F1` | Block and string: MOVS, FILL, CMPS, SCAN, SCANN |
| `F2` | Bitfield and bit-branch: EXTV, EXTZV, INSV, CMPV, BBS, BBC, BBSS, BBCC and the atomic forms |
| `F3` | Atomics: CAS, XADD, SWAP, ATAND, ATOR, ATXOR, CAS2 |
| `F4` | Conversions: MOVZ, MOVSX, integer to floating point and back |
| `F5` | Bit utilities and wide arithmetic: CLZ, CTZ, POPCNT, BSWAP, BREV, wide MUL, wide DIV |
| `F6` | System: MFSR, MTSR, TLBI, DCLEAN, DINVAL, DFLUSH, HALT, IOFENCE, UAON, UAOFF |
| `F7` | Control: SOB and variants, AOB, ACB, CASE |
| `F8` | Conditional values and hints: SETcc, CMOVcc, PAUSE, PREFETCH, PREFETCHW (section 25.8) |
| `F9`–`FB` | Reserved |

## 6. Condition codes

| Instructions | N, Z | V | C |
| --- | --- | --- | --- |
| ADD, SUB, CMP, ADC, SBB, NEG, INC, DEC | from result | signed overflow | carry out (add) or borrow (subtract) |
| Shifts and rotates | from result | cleared | last bit shifted out |
| MOV, AND, OR, XOR, BIC, NOT, TST, CLR | from result | cleared | unchanged |
| MUL, DIV | from result | set on overflow | unchanged |
| LEA, PUSH, POP, branches, calls, SYSCALL | unchanged | unchanged | unchanged |
| SOB, ACB | as the equivalent SUB or ADD and CMP |  |  |

The unsigned conditions follow the borrow convention: after `CMP a, b` the C flag is set if `b < a` unsigned (a borrow occurred).

**Floating-point compare** sets the flags so that the ordinary conditions work without extra rules:

| Relation | Flags | Branch to test it |
| --- | --- | --- |
| less | N=1, C=1 | MI |
| equal | Z=1 | EQ |
| greater | none set | GT |
| unordered (NaN) | V=1 | VS |

Ordered less-or-equal is LS, ordered greater-or-equal is GE, and NE is true when unordered.

## 7. Procedure calls

- `JSR dst` pushes the 8-byte return address and jumps. `RET` pops it and jumps. The return address is on the stack; there is no link register.
- `ENTER mask, locals`: pushes the callee-saved registers named in the 16-bit `mask` (bit i names R(16+i), pushed from the highest register number down), then pushes the 8-byte mask word, then pushes the old FP, then sets FP = SP and subtracts `locals` from SP.
- `EXIT`: reads the old FP and mask from the frame at FP, restores the registers named in the mask, restores SP and FP, and returns.
- `PUSHM mask` and `POPM mask` push and pop a register list with the same mask format.

Frame layout at FP: old FP at offset 0, mask at offset 8, saved registers from offset 16 upward, then the return address. Because the mask lives in the frame, `EXIT` has no operand and a debugger can unwind with no side tables.

```
func:   ENTER #0x0003, #32    ; save R16, R17; reserve 32 bytes of locals
        ...
        EXIT
```

## 8. Exceptions and restartability

**Rules.**

1. Every instruction is **restartable from its first byte**. A fault saves the PC of the faulting instruction, and the architectural registers look as though it never began.
2. Address computation uses the original register values. Register updates (including autoincrement and autodecrement) commit last, once no further fault is possible. Hardware that applies them early must be able to roll them back.
3. Multi-step instructions are idempotent until they commit. `ENTER` and `PUSHM` write below SP and update SP last. `EXIT` and `POPM` load everything first, then commit.
4. The one exception to rule 1 is a **long-running instruction** (block and string operations). They keep their progress in registers, so an interrupted instruction re-executes and carries on where it stopped.

**Trap frame.** On a trap the CPU pushes the PC and PSR (16 bytes) on the kernel stack. The cause code, faulting address and access information go into system registers, so the frame never varies in size. `RTE` pops the PC and PSR. `SYSCALL` and `BREAK` save the PC of the **next** instruction. Interrupts are accepted between instructions and at defined points inside interruptible instructions.

**Vectors (provisional).** The vector table is at VBR with one 8-byte handler address per vector. Vectors 0–63 are exceptions and 64–255 are device interrupts.

| Vector | Exception | Vector | Exception |
| --- | --- | --- | --- |
| 1 | Illegal instruction | 11 | Breakpoint |
| 2 | Reserved addressing mode | 12 | System call |
| 3 | Reserved operand | 13 | Bus error |
| 4 | Privileged instruction | 14 | Machine check |
| 5 | Instruction page fault | 15 | Floating point unavailable |
| 6 | Data read page fault | 17 | Single step |
| 7 | Data write page fault | 18 | Watchpoint |
| 8 | Access violation | 19 | Double fault |
| 9 | Divide error | 20 | Non-maskable interrupt |
| 10 | Floating-point exception | 64–66 | Timer, inter-processor interrupt, external interrupt |

Vectors 0, 16, 21–63 and 67–255 are reserved.

Interrupts have priority levels 1–7. An interrupt is taken only if its level is above the current IPL, and the handler runs at its own level. IPL 7 masks everything.

## 9. Privilege

Three modes: **kernel** (0), **server** (1) and **user** (2). Server mode is a **per-process** privilege: a server process runs in server mode in its own address space, and the kernel decides which processes get it. Calls between processes go through `SYSCALL` into the kernel.

**Banked stacks.** R31 is SP in every mode, but each mode has its own physical stack pointer, swapped automatically on a mode change. The inactive ones are reached with `MFSR` and `MTSR` (SPK, SPS, SPU) from kernel mode.

**What server mode adds over user mode:**

1. Access to pages whose PTE marks them server-accessible (memory-mapped devices for user-space drivers).
2. A small set of system registers, through `MFSR` and `MTSR` (below).
3. Direct delivery of interrupts that the kernel has routed to a server.
4. The cache-maintenance instructions `DCLEAN` and `DINVAL`, for DMA buffers.

It gets no access to the page-table roots, VBR, ASID or MMU control. Those stay kernel-only.

### 9.1 System registers (provisional)

| Register (number) | Access | Purpose |
| --- | --- | --- |
| ID (0) | Read-only, server and kernel | Architecture revision and feature bits (section 25.7) |
| CYCLES (1), PERFCTL (2), PERFCNT (3) | Server and kernel | Cycle counter and performance counters |
| SIMASK (4) | Server and kernel | Mask for interrupts routed to this server |
| CPUID (5), TIMEFREQ (6) | Read-only, server and kernel | CPU number; CYCLES frequency in Hz |
| TP (7) | Kernel (read from any mode with `MFTP`) | Thread pointer |
| ESP (8) | Kernel | Emergency stack for double fault, NMI and fatal machine checks |
| VBR (16) | Kernel | Vector table base |
| PTBR0 (17), PTBR1 (18) | Kernel | Root page tables for the user half and the kernel half |
| ASID (19), MMUCTL (20) | Kernel | Address-space ID (16 bits); bit 0 of MMUCTL enables translation |
| CAUSE (21), FAULTADDR (22), FAULTINFO (23) | Kernel | Trap details |
| SPK (24), SPS (25), SPU (26) | Kernel | Banked stack pointers |
| PSR (27) | Kernel | Processor status register (for context switches) |
| TIMECMP (28) | Kernel | Timer compare |
| MCSTATUS (29), MCADDR (30), MCCTL (31) | Kernel | Machine-check reporting |
| SCRATCH0–3 (32–35) | Kernel | Scratch registers for handlers |
| MPUCTL (40), PRBASE, PRLIM, PRATTR (64–111) | Kernel | Memory protection regions (profile E) |
| WPADDR0–3 (112–115), WPCTL0–3 (116–119) | Kernel | Hardware watchpoints |

### 9.2 Privileged instructions

`MFSR`, `MTSR`, `RTE`, `WAIT`, `HALT`, `TLBI` (kernel only); `DCLEAN`, `DINVAL` (server and kernel). `IFENCE`, `FENCE`, `SYSCALL` and `BREAK` are available in every mode. Executing a privileged instruction outside its allowed modes raises the privileged-instruction exception.

## 10. Virtual memory

- 64-bit virtual addresses, of which 48 bits are translated. Bits 63–48 must copy bit 47 (canonical form), otherwise the access faults. Bit 47 = 0 selects the user half (root in PTBR0) and bit 47 = 1 the kernel half (root in PTBR1).
- 4 KiB pages. A 4-level radix tree, 9 index bits per level, 8-byte entries. A leaf at level 1 maps 2 MiB and a leaf at level 2 maps 1 GiB.
- A hardware page-table walker fills the TLB. TLB entries are tagged with the ASID, and global pages ignore it.
- Page faults follow the restart rules in section 8.

**Page table entry (provisional):**

| Bits | Field | Meaning |
| --- | --- | --- |
| 0 | V | Valid |
| 1 | R | Readable |
| 2 | W | Writable |
| 3 | X | Executable |
| 4–5 | PRIV | Least-privileged mode allowed: 0 kernel only, 1 server and kernel, 2 everyone |
| 6 | A | Accessed (set by hardware) |
| 7 | D | Dirty (set by hardware) |
| 8 | G | Global |
| 9 | L | Leaf at levels 1 and 2 (large page) |
| 10–11 | SW | Reserved for software |
| 12–51 | PPN | Physical page number (52-bit physical addresses) |
| 52–53 | MT | Memory type: 0 normal write-back, 1 normal non-cacheable, 2 device, 3 reserved (section 25.2) |
| 54–63 |  | Reserved, zero |

## 11. Memory ordering

Strong ordering, in the style of x86 TSO. Stores from one CPU become visible to others in program order, and a CPU may only reorder a store followed by a later load to a different address. `FENCE` forces full ordering and is rarely needed. Atomic operations (`CAS`, `XADD`, `SWAP`) are fully ordered.

**Instruction fetch.** `IFENCE` (no operands, usable in every mode) guarantees that all earlier stores are visible to later instruction fetches and flushes the pipeline. Code that writes instructions must execute `IFENCE` before running them. On a multiprocessor, every other CPU that may execute the new code must also execute `IFENCE`. A hardware-coherent implementation may make it nearly free.

**DMA.** `DCLEAN` (write back) and `DINVAL` (invalidate) take an address and a length.

## 12. Floating point

- Formats: IEEE 754 binary16, binary32 and binary64, with binary128 optional. A binary128 value occupies an even-odd register pair (Fn, Fn+1).
- An F register is 64 bits. A binary16 or binary32 value sits in the low bits, and the upper bits are cleared on a write (section 25.1).
- The opcode (not the register) gives the operand type, as on the VAX.
- `FPSR` holds the rounding mode (nearest-even, toward zero, up, down), sticky exception flags (invalid, divide by zero, overflow, underflow, inexact) and trap-enable bits. `MFFS` reads it and `MTFS` writes it, in any mode.
- Compares set the integer flags as in section 6.

## 13. Complex instructions (provisional operand conventions)

**Block and string.** `MOVS.sz` copies R0 elements from (R1) to (R2) with memmove semantics. `FILL.sz value` fills R0 elements at (R2). `CMPS.sz` compares R0 elements at (R1) and (R2) and sets the flags. `SCAN.sz value` searches R0 elements from (R1) and sets Z if found, leaving R1 at the match. All keep progress in R0, R1 and R2, so they are interruptible.

**Bitfield.** `EXTV` and `EXTZV` (signed and unsigned extract), `INSV` (insert) and `CMPV` take a bit position, a size of 1 to 64 bits and a base. The base is a register or an address; with an address the position is a signed 64-bit bit offset from it, and the field may cross a page boundary. `BBS`, `BBC` and `BBSS` test a bit and branch, with short and long forms.

**Atomics.** `CAS.sz expected, new, addr`: if the value at addr equals expected, store new and set Z; otherwise load the value into expected and clear Z. `XADD.sz src, addr` adds src to memory and returns the old value in src. `SWAP.sz reg, addr` exchanges them. A misaligned address raises the reserved-operand fault.

**Bit utilities and wide arithmetic.** `CLZ`, `CTZ`, `POPCNT`, `BSWAP`, and rotates. Wide multiply gives a 128-bit product from two 64-bit sources into two registers. Wide divide divides a 128-bit value by a 64-bit divisor into a quotient and a remainder. Both are limited to 4 specifiers.

**Loops and switches.** `SOB Rn, target` subtracts one and branches if the result is not zero. `ACB limit, step, index, target` adds step to index, compares with limit and branches. `CASE value, base, limit` jumps through a table of 32-bit offsets after a bounds check.

**Conversions.** `MOVZ` and `MOVSX` zero- or sign-extend between sizes (the opcode carries both sizes). Integer-to-float and float-to-integer conversions are in the same group.

## 14. Assembler syntax

Source first, destination last. A size suffix follows the mnemonic.

```
ADD.l   #1, (R3)+        ; add 1 to a long, then advance R3
MOV.q   8(R30), R4       ; load from the frame
MOV.l   (R1)[R2], R5     ; array element: R1 + R2*4
CMP.l   #10, R0
BLT     loop
```

Register aliases: `SP` is R31 and `FP` is R30.

## 15. Reset and boot

- After reset the CPU is in kernel mode with IPL 7, translation off (physical addressing), and caches disabled or invalid.
- It reads an initial SP and an initial PC from two 8-byte words at a fixed physical address in the platform boot ROM (the address is a platform choice).
- PC, SP and PSR are defined. All other registers are undefined and must be written before being read.
- To enable the MMU, firmware builds the page tables, writes PTBR0 and PTBR1, then sets MMUCTL bit 0. The change takes effect on the next instruction fetch, so the code running at that moment must be identity-mapped.
- On a multiprocessor, one boot CPU runs and the others stay in reset until released by an inter-processor interrupt.
- Software reads the `ID` register to discover the architecture version and which optional features are present (binary128, vector extension, and so on).

## 16. Open items

1. Freeze the opcode assignments in sections 5.1, 5.2, 5.4 and 5.5 (the second-byte opcodes are now assigned in Appendix A).
2. Confirm that sub-64-bit register writes zero-extend (decided in section 25.1).
3. Review the floating-point literal table and FPSR layout (drafted in Appendix B).
4. Review the interrupt controller and inter-processor interrupt design (drafted in section 19).
5. Define the vector or matrix extension, to live in an escape page.
6. Confirm the floating-point trap model (draft: all traps disabled by default).
7. Write the reference assembler and a simulator, and use real kernels to measure code size and instruction mix before freezing the encoding.

## Appendix A. Second-byte opcode maps

Every group byte (`F0`–`F7`) is followed by a second opcode byte. Unless stated otherwise, bits 7–2 of the second byte select the operation and bits 1–0 give the operand size (`00` byte, `01` word, `10` long, `11` quad) or, for floating point, the type (Appendix B). Operands are source-first. Branch instructions come in a short form (8-bit displacement) and a long form (32-bit displacement), both after the specifiers.

### A.1 Block and string (`F1`)

Second byte = `4 * op + size`, where size is the element size. Fixed registers: **R0** is the element count, **R1** the source pointer, **R2** the destination pointer. All of these instructions are interruptible, with their progress left in R0–R2.

| op | Name | Operands | Effect |
| --- | --- | --- | --- |
| 0 | MOVS | none | Copy R0 elements from (R1) to (R2) with memmove semantics (copies backward when the areas overlap). Afterwards R0 = 0 and R1 and R2 point past the last element. |
| 1 | FILL | value | Store value into R0 elements starting at (R2). |
| 2 | CMPS | none | Compare the elements at (R1) and (R2) until they differ or R0 reaches 0. Flags are those of a CMP of the last pair; Z = 1 if all were equal. |
| 3 | SCAN | value | Search R0 elements from (R1) for value. Found: Z = 1 and R1 points at the match. Not found: Z = 0 and R0 = 0. |
| 4 | SCANN | value | As SCAN, but finds the first element that is not equal to value. |
| 5–63 |  |  | Reserved |

### A.2 Bitfield and bit-branch (`F2`)

The second byte is the plain operation number. Fields are 1 to 64 bits wide. When the base is a register, the bit position is taken modulo 64; when it is an address, the position is a signed bit offset from that address.

| op | Name | Operands | Effect |
| --- | --- | --- | --- |
| 0 | EXTV | pos, size, base, dst | Extract a field, sign-extended |
| 1 | EXTZV | pos, size, base, dst | Extract a field, zero-extended |
| 2 | INSV | src, pos, size, base | Insert the low bits of src into the field |
| 3 | CMPV | pos, size, base, value | Compare the sign-extended field with value; flags as CMP |
| 4, 5 | BBS | pos, base, target | Branch if the bit is set (short, long) |
| 6, 7 | BBC | pos, base, target | Branch if the bit is clear |
| 8, 9 | BBSS | pos, base, target | Test the bit, set it, branch if it was set |
| 10, 11 | BBCC | pos, base, target | Test the bit, clear it, branch if it was clear |
| 12, 13 | BBSSI | pos, base, target | As BBSS, but atomic |
| 14, 15 | BBCCI | pos, base, target | As BBCC, but atomic |
| 16–255 |  |  | Reserved |

The atomic forms act on the naturally aligned quad that contains the bit and are fully ordered.

### A.3 Atomics (`F3`)

Second byte = `4 * op + size`. The address must be naturally aligned, otherwise the reserved-operand fault is raised. All are fully ordered.

| op | Name | Operands | Effect |
| --- | --- | --- | --- |
| 0 | CAS | expected, new, addr | If the value at addr equals expected, store new and set Z. Otherwise load the value into expected and clear Z. |
| 1 | XADD | src, addr | Add src to memory and return the old value in src. Flags as ADD. |
| 2 | SWAP | reg, addr | Exchange reg with memory. |
| 3 | ATAND | src, addr | AND src into memory; the old value returns in src. |
| 4 | ATOR | src, addr | OR src into memory; the old value returns in src. |
| 5 | ATXOR | src, addr | XOR src into memory; the old value returns in src. |
| 6 | CAS2 | expected, new, addr | 16-byte compare-and-swap on even-odd register pairs (Fn, Fn+1 style, n even); addr must be 16-byte aligned. Z = 1 on success; on failure the current memory value loads into the expected pair. |
| 7–63 |  |  | Reserved |

### A.4 Conversions (`F4`)

Second byte = `16 * op + 4 * s + d`, where `s` is the source size or type and `d` the destination size or type. Integer sizes: 0 byte, 1 word, 2 long, 3 quad. Floating-point types: 0 binary16, 1 binary32, 2 binary64, 3 binary128.

| op | Name | Meaning |
| --- | --- | --- |
| 0 | MOVZ | Zero-extend an integer from size s to size d |
| 1 | MOVSX | Sign-extend an integer from size s to size d |
| 2 | CVTIF | Signed integer (size s) to floating point (type d) |
| 3 | CVTUF | Unsigned integer to floating point |
| 4 | CVTFI | Floating point (type s) to signed integer (size d), rounding toward zero |
| 5 | CVTFU | Floating point to unsigned integer, rounding toward zero |
| 6 | CVTFIR | Floating point to signed integer, using the FPSR rounding mode |
| 7 | CVTFF | Floating point type s to floating point type d |
| 8 | MOVIF | Move raw bits from an integer operand to an F register (source size must equal the width of type d) |
| 9 | MOVFI | Move raw bits from an F register to an integer operand |
| 10–15 |  | Reserved |

An out-of-range float-to-integer conversion saturates and raises the invalid-operation exception; NaN converts to 0. MOVZ, MOVSX, CVTFI, CVTFU and CVTFIR set N and Z from the integer result, clear V, and leave C unchanged. The other conversions leave the flags unchanged.

### A.5 Bit utilities and wide arithmetic (`F5`)

Second byte = `4 * op + size`.

| op | Name | Operands | Effect |
| --- | --- | --- | --- |
| 0 | CLZ | src, dst | Count leading zeros (the operand width for zero) |
| 1 | CTZ | src, dst | Count trailing zeros (the operand width for zero) |
| 2 | POPCNT | src, dst | Count set bits |
| 3 | BSWAP | src, dst | Reverse byte order (sizes word, long, quad) |
| 4 | BREV | src, dst | Reverse bit order |
| 8 | MULW | a, b, lo, hi | Signed product of two size-wide values; low half to lo, high half to hi |
| 9 | UMULW | a, b, lo, hi | Unsigned version of MULW |
| 10 | DIVW | divisor, Rn | Signed divide of the double-width value (R(n+1):Rn) by divisor; quotient to Rn, remainder to R(n+1) |
| 11 | UDIVW | divisor, Rn | Unsigned version of DIVW |
| others |  |  | Reserved |

MULW and UMULW set N from the high half and Z if the whole product is zero, and clear V and C. DIVW and UDIVW require Rn to be a register (R0–R30). A zero divisor, or a quotient that does not fit, raises the divide-error exception and changes no register.

### A.6 System (`F6`)

The second byte is the plain operation number.

| op | Name | Operands | Effect |
| --- | --- | --- | --- |
| 0 | MFSR | sreg, dst | Read a system register (sreg is a literal or immediate number) |
| 1 | MTSR | src, sreg | Write a system register |
| 2 | TLBI | kind, addr | Invalidate TLB entries: kind 0 all, 1 all for the current ASID, 2 entries for addr in every ASID, 3 entries for addr in the current ASID. For kinds 0 and 1 write `#0` for addr. |
| 3 | DCLEAN | addr, len | Write back cache lines in the range |
| 4 | DINVAL | addr, len | Invalidate cache lines in the range |
| 5 | DFLUSH | addr, len | Write back, then invalidate |
| 6 | HALT | none | Stop the CPU until reset |
| 7 | IOFENCE | none | Order device accesses against all other accesses (any mode) |
| 8 | UAON | none | Set PSR.UA (kernel and server only) |
| 9 | UAOFF | none | Clear PSR.UA (kernel and server only) |
| 10–255 |  |  | Reserved |

**System register numbers (provisional):** ID 0, CYCLES 1, PERFCTL 2, PERFCNT 3, SIMASK 4, VBR 16, PTBR0 17, PTBR1 18, ASID 19, MMUCTL 20, CAUSE 21, FAULTADDR 22, FAULTINFO 23, SPK 24, SPS 25, SPU 26, PSR 27, SCRATCH0–3 32–35. Access rights are in section 9.1; PSR is kernel-only.

### A.7 Control (`F7`)

The second byte is the plain operation number. All counters and limits are 64-bit signed values.

| op | Name | Operands | Effect |
| --- | --- | --- | --- |
| 0, 1 | SOB | Rn, target | Subtract one from Rn (flags as SUB) and branch if the result is not zero |
| 2, 3 | SOBGE | Rn, target | Subtract one and branch if the result is zero or positive |
| 4, 5 | SOBGT | Rn, target | Subtract one and branch if the result is positive |
| 6, 7 | AOBLT | limit, index, target | Add one to index and branch if index < limit |
| 8, 9 | AOBLE | limit, index, target | Add one to index and branch if index <= limit |
| 10, 11 | ACB | limit, step, index, target | Add step to index; branch if step >= 0 and index <= limit, or step < 0 and index >= limit |
| 12 | CASE | value, base, limit | Let t = value - base. If t <= limit (unsigned), jump to the table start plus the signed 32-bit entry t; otherwise continue after the table |
| 13–255 |  |  | Reserved |

For CASE, `limit` must be a literal or immediate (otherwise reserved-operand fault), so the instruction length is fixed. The table of (limit + 1) little-endian 32-bit offsets follows the specifiers, and offsets are relative to the start of the table.

## Appendix B. Floating-point instruction set

### B.1 Format

All floating-point instructions use group byte `F0`. The second byte is `4 * op + type`:

| type | Suffix | Format |
| --- | --- | --- |
| 0 | `.h` | binary16 |
| 1 | `.s` | binary32 |
| 2 | `.d` | binary64 |
| 3 | `.x` | binary128 (optional): an even-odd register pair Fn, Fn+1 with n even. Illegal instruction if the ID register says it is not implemented. |

A register-direct operand of floating-point type names an F register. Operands of integer type (in FCLASS, MFFS and MTFS) are ordinary integer operands. Only FCMP, FCMPE and FTST change the integer flags (as in section 6); every other floating-point instruction leaves them unchanged.

An exception sets its sticky flag in the FPSR. If the matching trap enable is set, the instruction raises vector 10 and changes no register. Otherwise it delivers the IEEE 754 default result.

### B.2 Operations

Two-operand forms follow the integer rule: `dst = dst op src`. In the three-operand forms the subtrahend and the divisor come first, as on the VAX: `FSUB3 a, b, dst` gives `dst = b - a` and `FDIV3 a, b, dst` gives `dst = b / a` (the integer SUB3 and DIV3 follow the same rule).

| op | Name | Operands | Effect |
| --- | --- | --- | --- |
| 0 | FMOV | src, dst | Copy |
| 1 | FADD | src, dst | Add |
| 2 | FSUB | src, dst | Subtract |
| 3 | FMUL | src, dst | Multiply |
| 4 | FDIV | src, dst | Divide |
| 5 | FCMP | a, b | Quiet compare; flags describe b relative to a, as CMP does (less means b < a). Raises invalid only for a signaling NaN. |
| 6 | FCMPE | a, b | Signaling compare; raises invalid for any NaN |
| 7 | FTST | src | Compare src with +0.0 |
| 8 | FNEG | src, dst | Negate |
| 9 | FABS | src, dst | Absolute value |
| 10 | FSQRT | src, dst | Square root |
| 11 | FMIN | src, dst | Minimum; NaN if either operand is NaN; -0.0 is less than +0.0 |
| 12 | FMAX | src, dst | Maximum, with the same rules |
| 13 | FCOPYSIGN | src, dst | dst takes the sign of src |
| 14 | FREM | src, dst | IEEE remainder of dst by src |
| 15 | FCLASS | src, dst | Integer dst receives a class mask: bit 0 -infinity, 1 negative normal, 2 negative subnormal, 3 -0, 4 +0, 5 positive subnormal, 6 positive normal, 7 +infinity, 8 signaling NaN, 9 quiet NaN |
| 16 | FRINT | src, dst | Round to an integral value using the FPSR rounding mode |
| 17 | FFLOOR | src, dst | Round toward minus infinity to an integral value |
| 18 | FCEIL | src, dst | Round toward plus infinity to an integral value |
| 19 | FTRUNC | src, dst | Round toward zero to an integral value |
| 20 | FADD3 | a, b, dst | dst = a + b |
| 21 | FSUB3 | a, b, dst | dst = b - a |
| 22 | FMUL3 | a, b, dst | dst = a \* b |
| 23 | FDIV3 | a, b, dst | dst = b / a |
| 24 | FMA | a, b, c, dst | dst = a \* b + c, one rounding |
| 25 | FMS | a, b, c, dst | dst = a \* b - c, one rounding |
| 26 | FNMA | a, b, c, dst | dst = c - a \* b, one rounding |
| 27 | FNMS | a, b, c, dst | dst = -(a \* b) - c, one rounding |
| 28 | MFFS | dst | Read the FPSR into an integer destination (type bits are 0) |
| 29 | MTFS | src | Write the FPSR from an integer source (type bits are 0) |
| 30–63 |  |  | Reserved |

### B.3 FPSR layout

| Bits | Field | Meaning |
| --- | --- | --- |
| 0–4 | Flags | Sticky exception flags: inexact (0), underflow (1), overflow (2), divide by zero (3), invalid operation (4). Cleared only by MTFS. |
| 8–12 | Trap enables | One per flag, in the same order (bit 8 inexact to bit 12 invalid) |
| 16–17 | RM | Rounding mode: 0 nearest-even, 1 toward zero, 2 toward +infinity, 3 toward -infinity |

All other bits are reserved and read as zero. After reset the FPSR is 0: round to nearest-even, all traps disabled.

### B.4 Floating-point short literals

When the operand type is floating point, short literal n (0–23) denotes the value below, rounded to the operand type. The assembler maps a constant such as `#1.0` to the short literal when it is in this table.

| n | Value | n | Value | n | Value |
| --- | --- | --- | --- | --- | --- |
| 0 | 0.0 | 8 | 10.0 | 16 | pi |
| 1 | 1.0 | 9 | 16.0 | 17 | pi/2 |
| 2 | 2.0 | 10 | 100.0 | 18 | e |
| 3 | 0.5 | 11 | 0.25 | 19 | ln 2 |
| 4 | 3.0 | 12 | 0.1 | 20 | sqrt(2) |
| 5 | 4.0 | 13 | -1.0 | 21 | +infinity |
| 6 | 5.0 | 14 | -2.0 | 22 | quiet NaN |
| 7 | 8.0 | 15 | -0.5 | 23 | -0.0 |

### B.5 Example

```
FADD.d   #1.0, F2          ; F2 = F2 + 1.0 (short literal 1)
FMA.d    F1, F2, F3, F4    ; F4 = F1 * F2 + F3, one rounding
FCMP.d   F0, F1            ; flags describe F1 relative to F0
BMI      less              ; taken if F1 < F0 (ordered)
```

## 17. Platform profiles

Kotl Upsilon is a mini-mainframe: one compact, stable, backward-compatible architecture that scales from small embedded controllers to multiprocessor servers and workstations. The base architecture (sections 1–15) is identical everywhere. **Profiles** only choose which optional features are present, and the ID register reports them. Software tests ID bits and never assumes a profile. Extensions use ID bits and the escape pages `FC`–`FF`; the base opcode space is never reused.

| Feature | E (embedded) | S (server) | W (workstation) |
| --- | --- | --- | --- |
| Address translation | Optional; memory protection regions required | Required | Required |
| Privilege modes | Kernel, user | Kernel, server, user, hypervisor | Kernel, server, user |
| Floating point | Optional | binary32 and binary64 required | binary32 and binary64 required |
| ECC and machine-check reporting | Optional | Required | Optional |
| Hypervisor extension | None | Required | Optional |
| Decimal extension | None | Expected | None |
| Multiprocessor | Optional | Required | Optional |
| Vector or matrix extension | None | Optional | Optional |

**Memory protection without translation (embedded).** With translation off, a profile E part protects physical memory with 16 regions. Each region has a base, a limit and an attribute word (R, W, X and the least-privileged mode allowed), held in the system registers MPUCTL (40) and PRBASE0–15, PRLIM0–15, PRATTR0–15 (64–111). A region matches when base <= address < limit, and the lowest-numbered matching region decides. No match in server or user mode is an access violation. Details are provisional.

## 18. Operating-system interface (ABI)

| Item | Rule (provisional) |
| --- | --- |
| Object format | ELF64, little-endian, with a private ELF machine number until one is registered |
| Function calls | Integer and pointer arguments in R0–R7, floating-point arguments in F0–F7, results in R0 (R1 for a second value) and F0. Further arguments go on the stack. SP is 16-byte aligned at every `JSR`, and there is no red zone below SP. Structures of 16 bytes or less travel in registers, larger ones by reference. |
| System calls | `SYSCALL #0`, with the number in R0 and arguments in R1–R6 (more on the stack). Results come back in R0 (R1 for a second value). C = 1 means error, with the errno in R0, as in the BSD convention. |
| Thread pointer | A system register TP (number 7), written by the kernel and read from any mode with `MFTP dst` (one-byte group, `EE`). Long-form mode kind 3 is `disp32(TP)`, so a thread-local variable is one operand. |
| Signals | The kernel pushes a frame (PC, PSR flags, R0–R31, F0–F31, FPSR) on the user stack and enters the handler through a trampoline that executes `SYSCALL` for the return. |
| Floating-point state | PSR bit 16 (FPD) disables floating point. Any floating-point instruction with FPD set raises vector 15 (floating point unavailable), so the kernel can switch floating-point state lazily. |
| Atomics | `CAS`, `XADD`, `SWAP`, `ATAND`, `ATOR`, `ATXOR`, plus `CAS2` (op 6 of the atomics group): a 16-byte compare-and-swap on an even-odd register pair at a 16-byte-aligned address. Because ordering is strong, most memory barriers reduce to compiler barriers. |
| Relocations | ABS64, ABS32, PCREL32, PCREL8 (short branch), GOT, PLT, TPOFF32 (local-exec TLS) and TLS initial-exec. They apply at any byte offset, since instructions are byte-aligned. |

## 19. Timers, interrupts, multiprocessing and platform

- **Timer.** CYCLES counts at TIMEFREQ Hz (system register 6, read-only, the same on every CPU). While CYCLES >= TIMECMP (system register 28, kernel), a timer interrupt at level 6 is pending on vector 64. Writing TIMECMP clears it. That is a complete system clock with no platform timer.
- **Interrupt controller (KIC).** A memory-mapped controller in the style of a simple PLIC: up to 1023 sources, each with a priority 0–7 (0 = off), per-CPU enable bits and a per-CPU claim/complete register. It presents the priority of the best pending source to the CPU as the interrupt level. The CPU takes it if the level is above the IPL, through the single external vector 66. The handler reads the claim register to get the source number and writes it back when done. The register map belongs to the platform specification.
- **Inter-processor interrupts.** A per-CPU software-interrupt register in the KIC raises vector 65 at level 5.
- **CPU number.** CPUID (system register 5, read-only) identifies the CPU. SCRATCH0 holds the per-CPU data pointer for trap entry.
- **Starting other CPUs.** Every CPU starts at the reset vector. CPUs with CPUID other than 0 wait until the boot CPU writes a start address into a platform mailbox and sends an inter-processor interrupt.
- **Platform description.** A flattened device tree (FDT) is passed in R0 (physical address) at kernel entry. It describes memory, CPUs, the KIC and the devices.
- **Console.** A memory-mapped NS16550-compatible UART.
- **Storage, network and entropy.** virtio-mmio devices. Virtio rings also give the architecture a channel-style I/O model: queues of descriptors that a device processes without the CPU.

## 20. Reliability, availability and serviceability (RAS)

- **Memory and caches.** Profile S requires ECC on memory and parity or ECC on caches.
- **Error classes.** Corrected errors are logged and never trap. Uncorrected but recoverable errors raise the machine-check vector (14), with the instruction restartable where possible. Fatal errors stop the CPU and signal the platform.
- **Reporting registers** (kernel): MCSTATUS (29), MCADDR (30) and MCCTL (31, enables per class).
- **Instruction retry.** On a transient internal fault the CPU retries the instruction once before reporting it (profile S).
- **Compatibility guarantee.** Binaries for the base architecture run unchanged on every profile. An extension instruction that is not implemented raises the illegal-instruction exception, and software finds out beforehand from the ID register.

## 21. Hypervisor extension (profile S)

- **Mode 3 is the hypervisor.** Guests run in their own kernel, server and user modes, each with its own PSR. Every instruction whose behaviour depends on privilege or machine state is already privileged, so a guest that executes one traps to the hypervisor, and plain trap-and-emulate works.
- **Nested translation.** A guest virtual address goes through the guest page tables (PTBR0 and PTBR1) to a guest physical address, then through the host table (HPTBR, same radix format) to a host physical address. TLB entries are tagged with the ASID and a 16-bit VMID.
- **Hypervisor-only system registers:** HPTBR, VMID, HCTL (which events trap to the hypervisor), HCAUSE and HVBR (its vector table).
- **Interrupts and time.** HCTL can raise virtual interrupts at chosen levels. A guest sees CYCLES plus a per-guest offset, and its TIMECMP is compared against that.
- This extension is outside the first NetBSD milestone.

## 22. Optional extensions

| Extension | Contents | Where |
| --- | --- | --- |
| Decimal | Packed-decimal arithmetic and conversion (`ADDP`, `SUBP`, `MULP`, `DIVP`, `CMPP`, `MOVP`, `CVTLP`, `CVTPL`) and an edit instruction, for COBOL and financial work | Escape page `FC` |
| Checksums and cryptography | CRC32, CRC32C, AES rounds, SHA-2 | Escape page `FD` |
| Vector and matrix | To be defined | Escape page `FE` |
| binary128 floating point | Appendix B type `.x` | ID bit |
| Hypervisor | Section 21 | ID bit |

## 23. NetBSD port plan

**Honest assessment.** Porting NetBSD to a new processor architecture is a major undertaking, and the first thing it needs is a working cross-toolchain (assembler, linker, C compiler). Only then can the kernel and userland be built. For Kotl Upsilon the toolchain is the critical path, not the kernel. My estimate is that a few weeks is realistic for the specification freeze, a simulator, and an assembler and linker. A compiler, a kernel that prints its banner, and a shell are more likely a matter of months for one person working part time. These estimates are mine and uncertain.

| Milestone | Done when | Estimate |
| --- | --- | --- |
| M0 Freeze a base subset ("v1.0 NetBSD profile") | Opcode map, ABI and ID register are frozen; extensions are excluded | 1–2 weeks |
| M1 Simulator | A C simulator runs bare-metal tests with UART, timer, KIC, an FDT and virtio-blk; a quick custom assembler produces the test programs | 2–4 weeks |
| M2 binutils port | `gas`, `ld` and `objdump` handle ELF64 for the ISA | 2–4 weeks (overlaps M1) |
| M3 GCC backend and runtime library | C compiles to ELF and runs on the simulator, including the compiler runtime helpers | 6–12 weeks if you have not written a backend before |
| M4 Kernel bring-up | The kernel reaches `main()` and prints its banner, with traps, pmap, clock and console working | 4–8 weeks |
| M5 Userland | A static `init` and shell run from a ramdisk, then from virtio-blk | 4–8 weeks |

**What to write for the kernel (the machine-dependent part):**

| Component | Notes |
| --- | --- |
| Trap and interrupt entry | Vector table, frame save and restore, syscall entry, `RTE` return |
| pmap | 4-level radix tables with 4 KiB pages and the PTE format in section 10; ASID handling and `TLBI` |
| Context switch | Switch registers, TP and (lazily, through FPD) floating-point state |
| `copyin`/`copyout` | Fault recovery through a per-LWP handler address |
| Interrupt priorities | NetBSD’s eight priority classes map onto the eight hardware IPLs |
| Clock | Timer interrupts from TIMECMP |
| Devices | NetBSD’s machine-independent FDT support, `com` driver for the UART and `virtio_mmio` driver; `bus_space` and `bus_dma` can be nearly trivial because DMA is coherent |
| Boot loader | Reads the kernel ELF from virtio-blk and jumps to it with the FDT address in R0 |
| libc | Syscall stubs, `setjmp`/`longjmp`, atomic operations, thread-pointer access, signal trampoline; start with static linking and add the dynamic loader later |

**Choices that save the most time:**

1. Build the toolchain as an external cross-compiler first and bring it into the NetBSD tree later. NetBSD ports have commonly started this way.
2. The compiler needs only the base subset: integer and floating-point instructions, the call sequence, branches, bit tests and `CAS`. Leave decimal, hypervisor, vector, binary128 and the string instructions to the assembler alone.
3. Model the flags in the compiler as an ordinary register that most instructions clobber, as the x86 backend does. The VAX and m68k backends are good references for the addressing modes, and NetBSD’s own `vax` and `m68k` ports show how a CISC port is laid out. The `riscv` and `aarch64` ports are the references for 4-level pmap, FDT boot and the interrupt framework.
4. Get everything working on the simulator before any hardware (FPGA) work.

**Sources.** [Porting NetBSD to the AMD x86-64](https://static.usenix.org/events/bsdcon/full_papers/linden/linden_html/index.html), [Porting NetBSD to a new ARM SoC](https://netbsd.org/docs/kernel/porting_netbsd_arm_soc.html), [Porting NetBSD to the RISC-V](https://www.bsdcan.org/2019/schedule/attachments/483_riscvnetbsd.pdf), [NetBSD virtio\_mmio.c](https://src.rcs.uwaterloo.ca/xref/netbsd-src/sys/dev/virtio/virtio_mmio.c).

## 24. Record of changes made to earlier sections (applied in v0.4)

| Section | Change |
| --- | --- |
| 2.3 PSR | Bit 16 is FPD (floating point disabled) |
| 4.3 long-form kinds | Kind 3 is `disp32(TP)` |
| 5.4 one-byte group | `EE` is `MFTP dst`; `EF` stays reserved |
| 8 vectors | 15 floating point unavailable, 64 timer, 65 inter-processor interrupt, 66 external interrupt |
| 9.1 system registers | Added CPUID 5, TIMEFREQ 6, TP 7, TIMECMP 28, MCSTATUS 29, MCADDR 30, MCCTL 31, MPUCTL 40, PRBASE/PRLIM/PRATTR 64–111 |
| A.3 atomics | Op 6 is CAS2 |

## 25. Gap closures (v0.4)

This section closes the design gaps found in review. Where it differs from older text, this section wins, and the earlier sections have been updated to match.

### 25.1 Register write rule

A write of byte, word or long size to a register **zero-extends** the result into all 64 bits. There is no partial-register state. This removes partial-register dependencies for any pipelined or out-of-order implementation and matches how 32-bit operations behave on x86-64 and AArch64. A write to a memory destination changes only its own bytes. Sign extension needs `MOVSX`. Flags always describe the result at the operand size. For floating point, a binary16 or binary32 write also clears the upper bits of the F register.

### 25.2 Memory types and device ordering

Each page (PTE bits 52–53) and each protection region (PRATTR bits 8–9) has a memory type MT:

| MT | Type | Behaviour |
| --- | --- | --- |
| 0 | Normal, write-back | Cacheable; accesses may be reordered, merged and speculated as usual |
| 1 | Normal, non-cacheable | Uncached; writes may be combined |
| 2 | Device | Uncached; no combining, no repeated or speculative accesses, and accesses to device memory happen in program order |
| 3 | Reserved |  |

Instruction fetch from device memory faults, and an unaligned access to device memory raises an access violation. `IOFENCE` (group `F6`, op 7, any mode) makes every earlier access complete before any later device access, and every earlier device access complete before any later access.

### 25.3 Kernel and user separation

- **Execution.** An instruction fetch is allowed only from a page whose PRIV field equals the current mode (kernel 0, server 1, user 2). The kernel can never execute user pages.
- **Data.** Data access to a page is allowed when PRIV is at least as privileged as the mode requires (PRIV >= mode number). A kernel or server access to a page with a larger PRIV (a less privileged page) additionally needs **PSR.UA = 1**; otherwise it raises an access violation. User mode is unaffected.
- **UA control.** `UAON` and `UAOFF` (group `F6`, ops 8 and 9; kernel and server only) set and clear UA. `copyin` and `copyout` style code brackets its user accesses with them. Trap entry clears UA, and the saved PSR keeps the old value, so a fault in between recovers cleanly.

### 25.4 Debugging support

- **Single step.** While PSR.TF is 1, every instruction that completes is followed by a trap on vector 17 with the PC of the next instruction. Trap entry clears TF and the saved PSR keeps it. `RTE` into a PSR with TF set executes exactly one instruction before the trap. `SYSCALL` and `BREAK` complete normally.
- **Hardware watchpoints.** Four watchpoints, WPADDR0–3 (system registers 112–115) and WPCTL0–3 (116–119). WPCTL bit 0 enables it, bits 1–2 give the length (0: 1 byte, 1: 2, 2: 4, 3: 8; the address must be aligned to it), bit 3 triggers on reads, bit 4 on writes, bit 5 on instruction fetch, and bits 6, 7, 8 select user, server and kernel mode. A hit raises vector 18 **before** the access commits, with the PC of the instruction, so a debugger disables the watchpoint, single-steps, and re-enables it.
- **Breakpoints.** `BREAK` raises vector 11 with the PC of the next instruction.

### 25.5 Emergency traps

- **Double fault (vector 19).** Raised when a fault occurs while delivering a trap, or in kernel mode with an unusable kernel stack. The CPU switches to the stack in ESP (system register 8) and enters the handler with the cause recorded in CAUSE. It is not expected to return.
- **Non-maskable interrupt (vector 20).** A platform signal (watchdog, fatal hardware event) that ignores the IPL. It runs on the ESP stack, and a second NMI is held until the first handler executes `RTE`.
- **Fatal machine checks** (section 20) also enter on the ESP stack.

### 25.6 Exception priority and trap sequence

Within one instruction, the first applicable exception below is taken:

1. Instruction-fetch faults (page fault, access violation, fetch watchpoint).
2. Illegal instruction, then reserved addressing mode.
3. Privileged instruction, then floating point unavailable (FPD set).
4. Operand faults in specifier order, left to right: reserved operand, then the page fault, access violation or watchpoint for each memory access in turn.
5. Arithmetic exceptions: divide error, and floating-point exceptions whose trap is enabled.
6. After the instruction completes: single step.

Between instructions the order is NMI, machine check, then interrupts by level (timer 6, inter-processor 5, external by its priority).

**Trap sequence.**

1. Select the kernel stack (the banked SP for kernel mode; a trap from kernel mode simply continues on it).
2. Push the PC, then the PSR (16 bytes in all).
3. Write CAUSE, FAULTADDR and FAULTINFO.
4. In the PSR: MODE = kernel, PMODE = previous mode, UA = 0, TF = 0. For an interrupt, IPL becomes the interrupt’s level; for an exception, IPL is unchanged.
5. Load the PC from the 8-byte entry at VBR + 8 \* vector.

If any step faults, the CPU takes the double fault. `RTE` pops the PSR, then the PC, and switches to the stack of the restored mode.

**Reserved bits and encodings.** Reserved bits in the PSR, FPSR and system registers read as zero, and writes to them are ignored. A reserved opcode, or a reserved system register number in `MFSR` or `MTSR`, raises the illegal-instruction exception.

### 25.7 ID register and versioning

The ID register (system register 0) is 64 bits and read-only:

| Bits | Field |
| --- | --- |
| 0–7 | Architecture revision (1 for this document) |
| 8–10 | Profile: 1 E, 2 S, 3 W, 0 unspecified |
| 11 | MMU present |
| 12 | MPU present |
| 13 | Floating point (binary32 and binary64) |
| 14 | binary128 |
| 15 | Hypervisor |
| 16 | Decimal extension |
| 17 | Checksum and cryptography extension |
| 18 | Vector and matrix extension |
| 19 | ECC and machine-check reporting |
| 20 | Multiprocessor |
| 32–35 | log2 of the cache line size in bytes |
| 40–45 | Physical address bits |
| 46–51 | Virtual address bits |

**Versioning rules.** Revisions only add features. No encoding is ever reused or changed. A reserved opcode stays an illegal instruction until a revision assigns it. Software discovers features through the ID bits, never by trial.

### 25.8 Conditional values and hints (group `F8`)

| Second byte | Name | Operands | Effect |
| --- | --- | --- | --- |
| `00`–`3F` | SETcc | dst | `4 * cond + size`: dst = 1 if the condition holds, else 0 (zero-extended); flags unchanged |
| `40`–`7F` | CMOVcc | src, dst | `0x40 + 4 * cond + size`: if the condition holds, dst = src; flags unchanged |
| `80` | PAUSE | none | Spin-wait hint |
| `81` | PREFETCH | addr | Hint to load the line for reading |
| `82` | PREFETCHW | addr | Hint to load the line for writing |
| others |  |  | Reserved |

Condition numbers are those of section 5.3, except that cond 15 is reserved here. The source of `CMOVcc` is always evaluated, so a memory source can fault even when the condition is false. Hints never fault and never trap.

### 25.9 Platform odds and ends

- **Power control.** A memory-mapped device in the device tree provides reset and power-off registers. Its register offsets belong to the platform specification.
- **Real-time clock.** A memory-mapped device in the device tree gives wall-clock time.
- **DWARF register numbers** (for debuggers and unwinders): R0–R31 are 0–31, F0–F31 are 32–63, and the return-address column is 64.

## Appendix C. Worked encodings

Hex bytes, in instruction-stream order. `d8` is an 8-bit displacement byte, `d32` a 32-bit one, little-endian.

| Assembly | Bytes | How it breaks down |
| --- | --- | --- |
| `RET` | `E1` | One-byte group |
| `CLR.q R0` | `6B 00` | Op 26, quad: 4 \* 26 + 3 = `6B`; R0 |
| `MOV.q R1, R2` | `03 01 02` | Op 0, quad; R1; R2 |
| `ADD.l #1, (R3)+` | `06 E1 43` | Op 1, long: 4 + 2; literal 1 = `E0 + 1`; `(R3)+` = mode 2, `40 + 3` |
| `MOV.q 8(FP), R4` | `03 9E 08 04` | `disp8(R30)` = `80 + 1E`, then the displacement; R4 |
| `MOV.l (R1)[R2], R5` | `02 C2 21 05` | Index prefix `C0 + 2`; base `(R1)` = `20 + 1`; R5 |
| `CMP.l #10, R0` | `0E EA 00` | Op 3, long: 12 + 2; literal 10 = `E0 + A`; R0 |
| `ADD3.l R1, R2, R3` | `82 01 02 03` | `0x80 + 4 * 0 + 2` |
| `BLT label` (short) | `C4 d8` | `0xC0 + 2 * 2 + 0` |
| `LEA.q label, R0` | `53 FE d32 00` | Op 20, quad: 83 = `53`; PC-relative = mode 7, field 30 = `FE` |
| `MOV.q #0x123456789ABCDEF0, R0` | `03 FB F0 DE BC 9A 78 56 34 12 00` | `imm64` = mode 7, field 27 = `FB`; value little-endian |
| `ENTER #3, #32` | `EA E3 F8 20` | Literal 3 = `E3`; 32 needs `imm8` = `F8`, then `20` |
| `SYSCALL #5` | `ED E5` | Literal 5 = `E5` |
| `MFTP R0` | `EE 00` | One-byte group |
| `FADD.d #1.0, F2` | `F0 06 E1 02` | Group `F0`; second byte `4 * 1 + 2`; floating-point literal 1; F2 |
| `MOVS.b` | `F1 00` | Group `F1`; op 0, byte |
| `CAS.q R1, R2, (R3)` | `F3 03 01 02 23` | Group `F3`; op 0, quad; R1; R2; `(R3)` = `20 + 3` |
| `BBS #3, R1, label` (short) | `F2 04 E3 01 d8` | Group `F2`; op 4; literal 3; R1; displacement |
| `SOB R3, label` (short) | `F7 00 03 d8` | Group `F7`; op 0; R3; displacement |
| `SETEQ.b R0` | `F8 00 00` | Group `F8`; `4 * 0 + 0`; R0 |
| `CMOVLT.q R1, R2` | `F8 4B 01 02` | `0x40 + 4 * 2 + 3`; R1; R2 |
| `PAUSE` | `F8 80` |  |
| `PREFETCH (R1)` | `F8 81 21` | `(R1)` = `20 + 1` |

## Revision notes

- v0.1: First consolidated draft. The short literal range is 0–23, because mode 7 is shared with the escape codes.

* v0.2: Named Kotl Upsilon. Added Appendix A (second-byte opcode maps) and Appendix B (floating-point instruction set, literal table and FPSR layout).

- v0.3: Added platform profiles, OS interface, timers and interrupts, RAS, hypervisor and optional extensions, the NetBSD port plan, and a list of changes to earlier sections (sections 17–24).

* v0.4: Closed the design gaps (section 25: register write rule, memory types, kernel and user separation, debugging, emergency traps, exception priority and trap sequence, ID register, conditional instructions and hints, platform odds and ends). Updated sections 2.1, 2.3, 4.3, 5.4, 5.5, 8, 9.1, 10, 12, A.3 and A.6 to match, and added Appendix C (worked encodings).
