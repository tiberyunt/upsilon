# Kotl Upsilon Assembly Programmer’s Guide

[Documentation license: CC BY-NC 4.0](LICENSE.md)

Oct 31, 2026 · @tiberiu

This guide shows how to write Kotl Upsilon assembly: the syntax, the addressing modes, the instructions you will use every day, the calling convention, and the idioms that keep code small. It follows Draft Specification v0.4; where the two differ, the specification wins. Directive names and a few operand spellings (absolute and memory-indirect addresses, two-size conversions) are assumptions, so adjust them to match your assembler.

## 1. The machine in one page

- **Registers.** 32 integer registers R0–R31 (64 bits), with `SP` = R31 and `FP` = R30, and 32 floating-point registers F0–F31. The PC is not a general register.
- **Flags.** N, Z, V and C. Most integer instructions set them (section 5).
- **Byte order and alignment.** Little-endian. Ordinary loads and stores may be unaligned. Atomic operations need natural alignment.
- **Sizes.** `.b` 8 bits, `.w` 16, `.l` 32, `.q` 64. A write of a size below 64 bits **zero-extends** into the whole register. Floating-point types are `.h` binary16, `.s` binary32, `.d` binary64 and `.x` binary128 (optional).
- **Operand order.** Source first: `ADD.l src, dst` means `dst = dst + src`.
- **Orthogonality.** Any operand of any instruction can use any addressing mode, apart from the exceptions noted below.
- **Instruction length.** 1 to 48 bytes, byte-aligned. Common instructions take 2–4 bytes.

| Registers | Role |
| --- | --- |
| R0–R7 | Arguments and results (caller-saved) |
| R8–R15 | Temporaries (caller-saved) |
| R16–R29 | Callee-saved |
| R30 (`FP`) | Frame pointer |
| R31 (`SP`) | Stack pointer |
| F0–F7 | Floating-point arguments and results |
| F0–F31 | All caller-saved |

## 2. Assembly syntax

| Element | Syntax | Example |
| --- | --- | --- |
| Comment | `;` to end of line | `; add one` |
| Label | `name:` | `loop:` |
| Register | `R0`–`R31`, `F0`–`F31`, `SP`, `FP` | `R3` |
| Immediate | `#value` (decimal, `0x` hex, negative allowed, or a label) | `#100`, `#-1`, `#0x1F` |
| FP immediate | `#value.fraction` | `#1.0`, `#0.5` |
| Size suffix | `.b .w .l .q` (integer), `.h .s .d .x` (FP) | `ADD.l`, `FADD.d` |
| Register deferred | `(Rn)` | `(R1)` |
| Autoincrement / autodecrement | `(Rn)+`, `-(Rn)` | `(R0)+` |
| Displacement | `d(Rn)` | `8(R1)`, `-8(FP)` |
| Indexed | `base[Rx]` (scaled by the operand size) | `(R1)[R2]`, `8(R1)[R2]` |
| PC-relative | a bare label | `LEA.q msg, R2` |
| Absolute | `@address` (assumed spelling) | `@0x1000` |
| Thread-pointer relative | `d(TP)` | `16(TP)` |
| Memory indirect | `*d(Rn)` (assumed spelling) | `*8(R1)` |
| Branch target | a label | `BNE loop` |

Directives follow the usual GNU-style names (assumed): `.text`, `.data`, `.bss`, `.globl name`, `.align n`, `.byte`, `.word`, `.long`, `.quad`, `.ascii`, `.asciz`, `.space n`, `.equ name, value`.

The assembler always picks the shortest encoding for immediates, displacements and branches.

## 3. Addressing modes

| Syntax | Meaning | Operand bytes |
| --- | --- | --- |
| `Rn` | Register | 1 |
| `(Rn)` | Memory at Rn | 1 |
| `(Rn)+` | Memory at Rn, then Rn += operand size | 1 |
| `-(Rn)` | Rn -= operand size, then memory at Rn | 1 |
| `d(Rn)` | Memory at Rn + d | 2 if d fits 8 bits, 3 if 16 bits, 6 if 32 bits |
| `base[Rx]` | Base address + Rx \* operand size | 1 + the base |
| `#n`, n = 0–23 | Short literal | 1 |
| `#n` otherwise | Immediate | 2 (8-bit), 3 (16-bit), 5 (32-bit), 9 (64-bit) |
| label | PC-relative address | 5 |
| `@addr` | Absolute address | 5 (32-bit) or 9 (64-bit) |
| `d(TP)` | Thread-local variable | 6 |

Rules worth remembering:

- Operands are evaluated left to right, and each one sees register changes made by the ones before it. `ADD.l (R3)+, (R3)+` reads two consecutive longs from the pointer in R3.
- Autoincrement steps by the operand size of **that** operand.
- An index prefix works only on memory modes, not on registers, literals, immediates, `(Rn)+` or `-(Rn)`.
- A literal or immediate can never be a destination, and `LEA` needs a memory operand.
- The index scale is always the operand size, so an array of 12-byte records needs a multiply and an `LEA`.

## 4. Instruction reference

### 4.1 Moving data

| Instruction | Effect |
| --- | --- |
| `MOV src, dst` | Copy |
| `CLR dst` | dst = 0 |
| `LEA addr, dst` | dst = the address of `addr` (quad) |
| `PUSH src`, `POP dst` | Push or pop a quad on the stack |
| `XCHG a, b` | Swap two operands |
| `MOVZ.xy src, dst` | Zero-extend from size x to size y (assumed spelling, for example `MOVZ.bq`) |
| `MOVSX.xy src, dst` | Sign-extend from size x to size y |

### 4.2 Integer arithmetic

| Instruction | Effect |
| --- | --- |
| `ADD src, dst`, `SUB src, dst` | dst = dst + src, dst = dst - src |
| `MUL src, dst`, `DIV src, dst` | dst = dst \* src, dst = dst / src (signed) |
| `INC dst`, `DEC dst`, `NEG src, dst`, `ABS src, dst` | Increment, decrement, negate, absolute value |
| `MIN src, dst`, `MAX src, dst` | dst = smaller or larger of dst and src |
| `ADC src, dst`, `SBB src, dst` | Add or subtract with carry (multi-word arithmetic) |
| `CMP a, b` | Compute b - a, set flags, store nothing |
| `TST src` | Set N and Z from src |
| `ADD3 a, b, dst` | dst = a + b |
| `SUB3 a, b, dst` | dst = b - a (the subtrahend comes first) |
| `MUL3`, `DIV3` | Same pattern; `DIV3 a, b, dst` gives dst = b / a |
| `MULW a, b, lo, hi` | Signed full product into two registers (`UMULW` unsigned) |
| `DIVW divisor, Rn` | Divide the pair R(n+1):Rn; quotient to Rn, remainder to R(n+1) (`UDIVW` unsigned) |

The three-operand forms also exist for AND, OR, XOR, BIC, the shifts and rotates, ADC and SBB.

### 4.3 Logic, shifts and bits

| Instruction | Effect |
| --- | --- |
| `AND`, `OR`, `XOR src, dst` | Bitwise operations |
| `BIC src, dst` | dst = dst AND NOT src |
| `NOT src, dst` | dst = NOT src |
| `SHL count, dst`, `SHR`, `SAR`, `ROL`, `ROR` | Shift left, logical right, arithmetic right, rotate (the count comes first) |
| `CLZ`, `CTZ`, `POPCNT src, dst` | Leading zeros, trailing zeros, set bits |
| `BSWAP src, dst`, `BREV src, dst` | Reverse bytes, reverse bits |
| `EXTZV pos, size, base, dst` | Extract a bit field, zero-extended (`EXTV` signed) |
| `INSV src, pos, size, base` | Insert into a bit field |
| `CMPV pos, size, base, value` | Compare a bit field with a value |
| `BBS pos, base, label` | Branch if the bit is set (`BBC` if clear) |
| `BBSS`, `BBCC` | Test, then set or clear the bit, and branch (`BBSSI`, `BBCCI` are atomic) |

A bit field is up to 64 bits wide. When `base` is a register the position is taken modulo 64; when it is a memory operand the position is a signed bit offset from that address.

### 4.4 Compare, branch, and conditional values

`CMP a, b` computes **b - a**, so `CMP #10, R0` followed by `BLT` branches when R0 < 10.

| Branch | Taken when | Use |
| --- | --- | --- |
| `BEQ` / `BNE` | Z / not Z | Equal / not equal |
| `BLT` `BLE` `BGT` `BGE` | N != V, Z or N != V, not Z and N = V, N = V | Signed comparisons |
| `BLO` `BLS` `BHI` `BHS` | C, C or Z, not C and not Z, not C | Unsigned comparisons (C is a borrow) |
| `BMI` / `BPL` | N / not N | Negative / non-negative |
| `BVS` / `BVC` | V / not V | Overflow set / clear (unordered after a floating-point compare) |
| `BRA` / `BSR` | Always | Jump / call by label |

Every branch has a short form (8-bit displacement, 2 bytes) and a long form (32-bit displacement, 5 bytes). The assembler chooses.

| Instruction | Effect |
| --- | --- |
| `SETcc.sz dst` | dst = 1 if the condition holds, else 0 (for example `SETLT.q R2`) |
| `CMOVcc.sz src, dst` | If the condition holds, dst = src. Flags are unchanged. |

`CMOVcc` always evaluates its source, so a memory source can fault even when the condition is false.

### 4.5 Loops and switches

| Instruction | Effect |
| --- | --- |
| `SOB Rn, label` | Rn -= 1; branch if the result is not zero |
| `SOBGE Rn, label`, `SOBGT Rn, label` | Same, branching if the result is >= 0 or > 0 |
| `AOBLT limit, index, label` | index += 1; branch if index < limit (`AOBLE` for <=) |
| `ACB limit, step, index, label` | index += step; branch if still within limit (up for positive step, down for negative) |
| `CASE value, base, limit` | Jump through a table of 32-bit offsets that follows the instruction; if value - base is above limit, continue after the table. `limit` must be a literal or immediate. |

### 4.6 Procedure calls

| Instruction | Effect |
| --- | --- |
| `JSR dst` | Push the return address and jump (any addressing mode, so indirect calls work) |
| `RET` | Pop the return address and jump |
| `ENTER #mask, #locals` | Save the callee-saved registers in `mask`, push the mask and old FP, set FP = SP, reserve `locals` bytes |
| `EXIT` | Undo `ENTER` and return |
| `PUSHM #mask`, `POPM #mask` | Push or pop a register list |
| `JMP dst` | Jump |

In the mask, **bit i names R(16+i)**.

### 4.7 Blocks and strings

These use fixed registers and can be interrupted and restarted: **R0 = element count, R1 = source pointer, R2 = destination pointer**. The size suffix is the element size.

| Instruction | Effect |
| --- | --- |
| `MOVS` | Copy R0 elements from (R1) to (R2), correct even if the areas overlap |
| `FILL value` | Store value into R0 elements at (R2) |
| `CMPS` | Compare elements at (R1) and (R2); Z = 1 if all equal |
| `SCAN value` | Search R0 elements from (R1); on a match Z = 1 and R1 points at it |
| `SCANN value` | Find the first element that differs from value |

### 4.8 Atomics and ordering

Addresses must be naturally aligned. All atomics are fully ordered.

| Instruction | Effect |
| --- | --- |
| `CAS expected, new, addr` | If (addr) equals expected, store new and set Z; otherwise load (addr) into expected and clear Z |
| `XADD src, addr` | Add src to memory; src receives the old value |
| `SWAP reg, addr` | Exchange |
| `ATAND`, `ATOR`, `ATXOR src, addr` | Atomic logic; src receives the old value |
| `CAS2` | 16-byte compare-and-swap on an even-odd register pair |
| `FENCE`, `IFENCE`, `IOFENCE` | Full memory barrier; make written code visible to instruction fetch; order device accesses |

Ordinary stores are seen in program order (strong ordering), so a plain store releases a lock. Only a store followed by a load to another address can be reordered, and `FENCE` prevents that.

### 4.9 Floating point

Operations take a type suffix: `FADD.d F1, F2`. Registers hold one value; the low bits hold `.h` and `.s` values and the rest is cleared.

| Instruction | Effect |
| --- | --- |
| `FMOV`, `FADD`, `FSUB`, `FMUL`, `FDIV src, dst` | dst = dst op src |
| `FADD3`, `FSUB3`, `FMUL3`, `FDIV3 a, b, dst` | dst = a + b, b - a, a \* b, b / a |
| `FMA a, b, c, dst` | dst = a \* b + c with one rounding (`FMS`, `FNMA`, `FNMS` variants) |
| `FNEG`, `FABS`, `FSQRT`, `FMIN`, `FMAX`, `FCOPYSIGN`, `FREM` | As named |
| `FRINT`, `FFLOOR`, `FCEIL`, `FTRUNC src, dst` | Round to an integral value |
| `FCMP a, b`, `FCMPE a, b`, `FTST src` | Compare and set the integer flags |
| `CVTIF`, `CVTFI`, `CVTFF`, `MOVIF`, `MOVFI` | Convert between integer and floating-point types, or move raw bits |
| `MFFS dst`, `MTFS src` | Read or write the FPSR |

Branching after a floating-point compare (`FCMP a, b` describes b relative to a):

| Test | Branch |
| --- | --- |
| b == a | `BEQ` |
| b != a (also true if unordered) | `BNE` |
| b < a | `BMI` |
| b <= a | `BLS` |
| b > a | `BGT` |
| b >= a | `BGE` |
| Unordered (a NaN) | `BVS` |

Short literals work for common floating-point constants: `FADD.d #1.0, F2` costs one byte for the constant.

### 4.10 System and miscellaneous

| Instruction | Effect |
| --- | --- |
| `SYSCALL #n` | Enter the kernel (see section 6) |
| `BREAK` | Debugger breakpoint |
| `MFTP dst` | Read the thread pointer |
| `PAUSE` | Spin-wait hint |
| `PREFETCH addr`, `PREFETCHW addr` | Cache hints that never fault |
| `NOP` | Nothing |

## 5. Flags

| Instructions | N, Z | V | C |
| --- | --- | --- | --- |
| `ADD`, `SUB`, `CMP`, `ADC`, `SBB`, `NEG`, `INC`, `DEC` | From the result | Signed overflow | Carry out (add) or borrow (subtract) |
| Shifts and rotates | From the result | Cleared | Last bit shifted out |
| `MOV`, `AND`, `OR`, `XOR`, `BIC`, `NOT`, `TST`, `CLR` | From the result | Cleared | Unchanged |
| `MUL`, `DIV` | From the result | Set on overflow | Unchanged |
| `LEA`, `PUSH`, `POP`, branches, calls, `SETcc`, `CMOVcc`, `SYSCALL` | Unchanged | Unchanged | Unchanged |

Flags always describe the result at the operand size. Because `LEA` and `CMOVcc` leave the flags alone, you can compute a pointer or select a value between a compare and its branch.

## 6. Procedure calls and system calls

**Calling convention.**

- Integer and pointer arguments go in R0–R7, floating-point arguments in F0–F7, and extra arguments on the stack.
- Results come back in R0 (and R1 for a second value) and F0.
- The caller removes stacked arguments after the call.
- SP is 16-byte aligned at every `JSR`. There is no red zone below SP.
- Structures of 16 bytes or less travel in registers; larger ones go by reference.

**Stack frame after `ENTER`.** Old FP at `0(FP)`, the mask at `8(FP)`, the saved registers from `16(FP)` upward, then the return address, then stacked arguments. Locals live below FP at `-8(FP)`, `-16(FP)` and so on.

**System calls.** Put the call number in R0 and the arguments in R1–R6, then `SYSCALL #0`. On return R0 holds the result (R1 a second value). **C = 1 means an error**, and R0 then holds the error number. Test it with `BLO`.

## 7. Worked examples

**Sum an array of 32-bit integers** (R0 = pointer, R1 = count; returns the sum modulo 2^32 in R0):

```
sum32:
        CLR.q   R2
        TST.q   R1
        BEQ     done
again:  ADD.l   (R0)+, R2       ; add the next element, advance the pointer
        SOB     R1, again       ; count--, loop while not zero
done:   MOV.q   R2, R0
        RET
```

**String length** using `SCAN` (R0 = string):

```
strlen:
        MOV.q   R0, R1          ; SCAN searches from R1
        MOV.q   R0, R3          ; remember the start
        MOV.q   #-1, R0         ; count = as many as it takes
        SCAN.b  #0              ; find the terminating zero byte
        SUB.q   R3, R1          ; length = end - start
        MOV.q   R1, R0
        RET
```

**Memory copy** (R0 = destination, R1 = source, R2 = length; returns the destination). `MOVS` wants R0 = count and R2 = destination, so swap them:

```
memcpy:
        MOV.q   R0, R3          ; keep the return value
        XCHG.q  R0, R2          ; R0 = count, R2 = destination, R1 = source
        MOVS.b                  ; copy bytes (use .q on aligned bulk data)
        MOV.q   R3, R0
        RET
```

**Recursive factorial** with `ENTER` and `EXIT`:

```
fact:   ENTER   #0x0001, #0     ; save R16
        MOV.q   R0, R16         ; keep n
        CMP.q   #2, R0          ; flags from R0 - 2
        BLT     base            ; n < 2
        DEC.q   R0
        JSR     fact            ; R0 = fact(n - 1)
        MUL.q   R16, R0         ; R0 = R0 * n
        EXIT
base:   MOV.q   #1, R0
        EXIT
```

**Locals in a frame:**

```
func:   ENTER   #0, #16         ; no saved registers, 16 bytes of locals
        MOV.q   R0, -8(FP)
        MOV.q   R1, -16(FP)
        ; ...
        EXIT
```

**Atomic counter and spin lock:**

```
atomic_inc:                     ; R0 = address of a quad; returns the old value
        MOV.q   #1, R1
        XADD.q  R1, (R0)
        MOV.q   R1, R0
        RET

lock:                           ; R0 = address of a lock word (0 = free)
        MOV.q   #1, R1
retry:  SWAP.q  R1, (R0)        ; atomic exchange
        TST.q   R1
        BEQ     got             ; old value was 0: acquired
        PAUSE
        BRA     retry
got:    RET

unlock: CLR.q   (R0)            ; strong ordering: a plain store releases
        RET
```

**Dot product of two double arrays** (R0 = a, R1 = b, R2 = n; result in F0):

```
dot:    FMOV.d  #0.0, F0
        TST.q   R2
        BEQ     done
loop:   FMOV.d  (R0)+, F1
        FMA.d   F1, (R1)+, F0, F0   ; F0 = F1 * b[i] + F0
        SOB     R2, loop
done:   RET
```

**Switch with `CASE`** (R0 = 0..3):

```
        CASE    R0, #0, #3
table:  .long   case0 - table
        .long   case1 - table
        .long   case2 - table
        .long   case3 - table
        BRA     default         ; reached when R0 is out of range
```

**Bit fields and conditional values:**

```
        EXTZV   #4, #8, R1, R0  ; R0 = bits 4..11 of R1
        INSV    R2, #4, #8, R1  ; put R2's low 8 bits into bits 4..11 of R1

        CMP.q   R1, R0          ; flags from R0 - R1
        CMOVGT.q R1, R0         ; if R0 > R1 then R0 = R1  (a signed minimum)
        SETLT.q R2              ; R2 = (R0 < R1)
```

**Hello world:**

```
        .equ    SYS_write, 4    ; the number comes from your OS headers
        .equ    SYS_exit, 1
        .data
msg:    .ascii  "hello"
        .text
        .globl  _start
_start: MOV.q   #SYS_write, R0
        MOV.q   #1, R1          ; file descriptor
        LEA.q   msg, R2
        MOV.q   #5, R3
        SYSCALL #0
        BLO     fail            ; C = 1: error number in R0
        MOV.q   #SYS_exit, R0
        CLR.q   R1
        SYSCALL #0
fail:   MOV.q   R0, R1
        MOV.q   #SYS_exit, R0
        SYSCALL #0
```

## 8. Writing compact code

| Instead of | Write | Size |
| --- | --- | --- |
| `MOV.q #0, R0` | `CLR.q R0` | 2 bytes instead of 3 |
| `CMP.q #0, R1` | `TST.q R1` | 2 bytes instead of 3 |
| `MOV.q R2, R3` then `ADD.q R1, R3` | `ADD3.q R1, R2, R3` | 4 bytes instead of 6 |
| A load and a separate pointer increment | `MOV.l (R1)+, R2` | 3 bytes instead of 6 |
| `DEC.q R1` and `BNE` | `SOB R1, label` | 4 bytes instead of 5 |
| A 64-bit constant in a register | `LEA.q label, R0` (7 bytes) | 11 bytes for `MOV.q #imm64, R0` |
| Constants above 23 | Keep them in 8 bits when you can | `ADD.q #100, R0` is 4 bytes; `#1000` is 5 |
| Large struct offsets | Keep hot fields within 127 bytes of the base | 2-byte operand, not 3 or 6 |

Other habits that help: keep frequently used branch targets within 127 bytes, prefer `.q` operations on 64-bit data to avoid extra conversions, and use `MIN`, `MAX`, `ABS` and `CMOVcc` instead of branches around single moves.

## 9. Pitfalls

| Pitfall | What happens |
| --- | --- |
| Sub-64-bit writes | `ADD.b #1, R0` clears bits 8–63 of R0. Use `.q` when the register holds a wider value. |
| `CMP` direction | `CMP a, b` tests b relative to a, so `CMP #10, R0; BLT` means R0 < 10. |
| `SUB3` and `DIV3` order | The subtrahend or divisor comes first: `SUB3 a, b, dst` gives b - a. |
| Carry is a borrow | After a subtraction or compare, C = 1 means the unsigned result went below zero. Use `BLO`/`BHS`, not signed branches, for unsigned data. |
| Same register twice | `(R3)+, (R3)+` is defined (left to right), but easy to misread. |
| `CMOVcc` memory source | It is read even when the condition is false and can fault. |
| Immediate as destination | A literal or immediate destination raises the reserved-operand fault. |
| Misaligned atomics | `CAS`, `XADD`, `SWAP` and the atomic bit-branches fault on a misaligned address. |
| Self-modifying code | Execute `IFENCE` after writing instructions, and on every CPU that will run them. |
| Device registers | Use `IOFENCE` to order device accesses with normal memory. |
| String instructions | `MOVS`, `CMPS` and `SCAN` use R0–R2 and change them. |
| `LEA` of a register | `LEA` needs a memory operand; a register operand faults. |

## 10. Opcode map at a glance

| First byte | Meaning |
| --- | --- |
| `00`–`7F` | Two-operand integer ops: opcode = 4 \* op + size (`.b .w .l .q` = 0–3) |
| `80`–`BF` | Three-operand integer ops: opcode = 0x80 + 4 \* op + size |
| `C0`–`DF` | Branches: opcode = 0xC0 + 2 \* condition + (0 short, 1 long) |
| `E0`–`EF` | One-byte instructions: NOP, RET, EXIT, RTE, IFENCE, FENCE, BREAK, WAIT, JMP, JSR, ENTER, PUSHM, POPM, SYSCALL, MFTP |
| `F0` | Floating point |
| `F1`–`F8` | Strings, bitfields, atomics, conversions, bit utilities and wide arithmetic, system, control, conditional values and hints |
| `F9`–`FF` | Reserved and escape pages for extensions |

The Draft Specification has the full tables and the worked byte encodings (Appendix C).
