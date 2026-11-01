"""Transactional reference CPU, physical RAM, page walker, and trap delivery."""
import math
import struct
from dataclasses import dataclass
from fractions import Fraction
from .isa import *
from .decoder import decode
from .floating import quantize, sqrt_quantize

PSR_MASK = 0x7F70F
FP_INEXACT, FP_UNDERFLOW, FP_OVERFLOW, FP_DIVZERO, FP_INVALID = (1 << i for i in range(5))

class Memory:
    def __init__(self, size=1 << 20, *, serial=None):
        if size <= 0:
            raise ValueError('RAM size must be positive')
        self.data = bytearray(size)
        self.serial = serial
        self.rom_ranges = []
        if serial is not None and size > serial.BASE:
            raise ValueError('RAM overlaps the serial port')

    def check(self, address, size, *, write=False):
        if address < 0 or size < 0 or address + size > len(self.data):
            raise Fault(8, f'physical access outside RAM at 0x{address:x}', address)
        if write and any(address < end and address + size > start for start, end in self.rom_ranges):
            raise Fault(8, 'write to ROM', address, 2)

    def load_rom(self, address, data):
        self.load(address, data)
        self.rom_ranges.append((address, address + len(data)))

    def is_serial(self, address):
        return self.serial is not None and self.serial.BASE <= address < self.serial.BASE + self.serial.SIZE

    def load(self, address, data):
        self.check(address, len(data))
        self.data[address:address + len(data)] = data

    def read(self, address, size):
        self.check(address, size)
        return int.from_bytes(self.data[address:address + size], 'little')

@dataclass
class Reference:
    kind: str
    value: int
    size: int
    floating: bool = False

from .fp_cpu import FloatingCPU

class CPU(FloatingCPU):
    def __init__(self, memory=None, *, handle_traps=False):
        self.memory = memory if memory is not None else Memory()
        self.r, self.f = [0] * 32, [0] * 32
        self.pc, self.psr, self.fpsr = 0, 7 << 8, 0
        self.sys = {name: 0 for name in SYSTEM_REGISTERS}
        self.sys['ID'] = (48 << 46) | (52 << 40) | (6 << 32) | 0x3b01
        self.r[31] = len(self.memory.data) & ~15
        self.sys['SPK'] = self.r[31]
        self.halted, self.waiting = False, False
        self.steps, self.handle_traps = 0, handle_traps
        self.last_fault = None
        self.interrupts = []
        self._writes = None
        self._io_actions = []
        self._rx_reads = 0
        self._blocks = {}

    @property
    def mode(self):
        return (self.psr >> 12) & 3

    def load(self, program):
        self.memory.load(program.origin, program.code)
        self.pc = program.entry
        self.halted = self.waiting = False

    def reset(self, boot_address=0):
        sp, pc = self.memory.read(boot_address, 8), self.memory.read(boot_address + 8, 8)
        self.__init__(self.memory, handle_traps=self.handle_traps)
        self.r[31], self.sys['SPK'], self.pc = sp, sp, pc

    def snapshot(self):
        return (self.r[:], self.f[:], self.pc, self.psr, self.fpsr, self.sys.copy(), self.halted, self.waiting, self._blocks.copy())

    def restore(self, state):
        self.r, self.f, self.pc, self.psr, self.fpsr, self.sys, self.halted, self.waiting, self._blocks = state

    def physical_read(self, address, size):
        if self.memory.is_serial(address):
            serial = self.memory.serial
            if size != 1:
                raise Fault(8, 'serial registers require byte access', address, 1)
            offset = address - serial.BASE
            value = serial.peek(offset, self._rx_reads if self._writes is not None else 0)
            if offset == serial.DATA and (self._rx_reads if self._writes is not None else 0) < len(serial.input):
                if self._writes is None:
                    serial.receive()
                else:
                    self._rx_reads += 1
                    self._io_actions.append(('rx', 0))
            return value
        self.memory.check(address, size)
        return int.from_bytes(bytes(self._writes.get(address + i, self.memory.data[address + i])
                                    if self._writes is not None else self.memory.data[address + i]
                                    for i in range(size)), 'little')

    def physical_write(self, address, value, size):
        if self.memory.is_serial(address):
            serial = self.memory.serial
            if size != 1:
                raise Fault(8, 'serial registers require byte access', address, 2)
            serial.check(address - serial.BASE, write=True)
            if self._writes is None:
                serial.transmit(value)
            else:
                self._io_actions.append(('tx', value & 255))
            return
        self.memory.check(address, size, write=True)
        raw = (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little')
        if self._writes is None:
            self.memory.load(address, raw)
        else:
            self._writes.update((address + i, byte) for i, byte in enumerate(raw))

    def translate(self, address, access):
        address &= MASK64
        if not self.sys['MMUCTL'] & 1:
            return address
        high = address >> 48
        if high != (0xFFFF if address & (1 << 47) else 0):
            raise Fault(8, 'noncanonical virtual address', address, access)
        root = self.sys['PTBR1' if address & (1 << 47) else 'PTBR0']
        table = root & 0x000FFFFFFFFFF000
        vector = {1: 6, 2: 7, 4: 5}[access]
        for level, shift in enumerate((39, 30, 21, 12)):
            pte_address = table + ((address >> shift) & 511) * 8
            try:
                pte = self.physical_read(pte_address, 8)
            except Fault as exc:
                raise Fault(13, 'page table outside physical RAM', address, access) from exc
            privilege = (pte >> 4) & 3
            if not pte & 1 or pte >> 52 or privilege == 3:
                raise Fault(vector, 'invalid page table entry', address, access)
            leaf = level == 3 or level in (1, 2) and bool(pte & (1 << 9))
            if level == 0 and pte & (1 << 9):
                raise Fault(vector, 'invalid large page level', address, access)
            if leaf:
                if self.mode > privilege or not pte & {1: 2, 2: 4, 4: 8}[access]:
                    raise Fault(8, 'page permission violation', address, access)
                base = pte & 0x000FFFFFFFFFF000
                if base & ((1 << shift) - 1):
                    raise Fault(vector, 'misaligned large page', address, access)
                self.physical_write(pte_address, pte | (1 << 6) | ((1 << 7) if access == 2 else 0), 8)
                return base + (address & ((1 << shift) - 1))
            self.physical_write(pte_address, pte | (1 << 6), 8)
            table = pte & 0x000FFFFFFFFFF000
        raise Fault(vector, 'unmapped address', address, access)

    def read(self, address, size, access=1):
        value = 0
        for i in range(size):
            virtual = (address + i) & MASK64
            physical = self.translate(virtual, access)
            if access == 4 and self.memory.is_serial(physical):
                raise Fault(8, 'instruction fetch from serial port', virtual, access)
            try:
                value |= self.physical_read(physical, 1) << (i * 8)
            except Fault as exc:
                raise Fault(13 if self.sys['MMUCTL'] & 1 else 8, str(exc), virtual, access) from exc
        return value

    def write(self, address, value, size):
        # Translation and every bounds check precede the first physical write.
        physical = []
        for i in range(size):
            virtual = (address + i) & MASK64
            addr = self.translate(virtual, 2)
            try:
                if self.memory.is_serial(addr):
                    self.memory.serial.check(addr - self.memory.serial.BASE, write=True)
                else:
                    self.memory.check(addr, 1, write=True)
            except Fault as exc:
                raise Fault(13 if self.sys['MMUCTL'] & 1 else 8, str(exc), virtual, 2) from exc
            physical.append(addr)
        for i, addr in enumerate(physical):
            self.physical_write(addr, value >> (i * 8), 1)

    def resolve(self, op, size, end, floating=False):
        mode, reg = op.mode, op.reg
        if mode == 0:
            return Reference('reg', reg, size, floating)
        if mode == 7 and not op.kind and reg < 28:
            if floating:
                if reg < 24:
                    return Reference('float', self.bits_float(quantize(FP_CONSTANTS[reg], size)[0], size), size, True)
                if reg not in (26, 27):
                    raise Fault(3, 'floating immediate must be binary32 or binary64')
                width = 4 if reg == 26 else 8
                value = struct.unpack('<f' if width == 4 else '<d', (op.value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little'))[0]
                return Reference('float', value, size, True)
            return Reference('imm', reg if reg < 24 else op.value, size)
        if mode in (1, 2):
            address = self.r[reg]
            if mode == 2:
                self.r[reg] = (address + size) & MASK64
        elif mode == 3:
            address = self.r[reg] = (self.r[reg] - size) & MASK64
        elif mode in (4, 5) or op.kind:
            address = (self.r[reg] + op.value) & MASK64
            if op.kind == 3:
                address = self.read(address, 8)
        elif mode == 7:
            address = (end + op.value if reg == 30 else op.value) & MASK64
        else:
            raise Fault(2, 'reserved addressing mode')
        if op.index is not None:
            address = (address + self.r[op.index] * size) & MASK64
        return Reference('mem', address, size, floating)

    def raw(self, ref):
        mask = (1 << (ref.size * 8)) - 1
        if ref.kind == 'reg':
            return (self.f if ref.floating else self.r)[ref.value] & mask
        if ref.kind == 'mem':
            return self.read(ref.value, ref.size)
        if ref.kind == 'imm':
            return ref.value & mask
        return self.float_bits(ref.value, ref.size)

    def value(self, ref):
        if ref.kind == 'float':
            return ref.value
        value = self.raw(ref)
        return self.bits_float(value, ref.size) if ref.floating else value

    def store(self, ref, value, *, raw=False):
        if ref.kind not in ('reg', 'mem'):
            raise Fault(3, 'literal or immediate destination')
        if ref.floating and not raw:
            value = self.float_bits(value, ref.size)
        mask = (1 << (ref.size * 8)) - 1
        value &= mask
        if ref.kind == 'reg':
            regs = self.f if ref.floating else self.r
            regs[ref.value] = value
        else:
            self.write(ref.value, value, ref.size)

    @staticmethod
    def bits_float(value, size):
        return struct.unpack({2: '<e', 4: '<f', 8: '<d'}[size], value.to_bytes(size, 'little'))[0]

    @staticmethod
    def float_bits(value, size):
        fmt = {2: '<e', 4: '<f', 8: '<d'}[size]
        try:
            return int.from_bytes(struct.pack(fmt, value), 'little')
        except OverflowError:
            return int.from_bytes(struct.pack(fmt, math.copysign(math.inf, value)), 'little')

    def push(self, value, size=8):
        self.r[31] = (self.r[31] - size) & MASK64
        self.write(self.r[31], value, size)

    def pop(self, size=8):
        value = self.read(self.r[31], size)
        self.r[31] = (self.r[31] + size) & MASK64
        return value

    def flags(self, result, size, overflow=False, carry=None):
        result &= (1 << (size * 8)) - 1
        self.psr &= ~(N | Z | V | (C if carry is not None else 0))
        self.psr |= (N if result & (1 << (size * 8 - 1)) else 0) | (Z if result == 0 else 0) | (V if overflow else 0)
        if carry:
            self.psr |= C

    def arithmetic(self, name, a, b, size):
        bits, mask = size * 8, (1 << (size * 8)) - 1
        sa, sb = signed(a, size), signed(b, size)
        carry, overflow = None, False
        if name in ('ADD', 'ADC'):
            extra = int(bool(self.psr & C)) if name == 'ADC' else 0
            result = b + a + extra
            overflow = not -(1 << (bits - 1)) <= sb + sa + extra < (1 << (bits - 1))
            carry = result > mask
        elif name in ('SUB', 'CMP', 'SBB'):
            extra = int(bool(self.psr & C)) if name == 'SBB' else 0
            result = b - a - extra
            overflow = not -(1 << (bits - 1)) <= sb - sa - extra < (1 << (bits - 1))
            carry = b < a + extra
        elif name in ('MUL', 'DIV'):
            if name == 'MUL':
                result = sb * sa
            else:
                if a == 0:
                    raise Fault(9, 'division by zero')
                result = (abs(sb) // abs(sa)) * (-1 if (sb < 0) != (sa < 0) else 1)
            overflow = not -(1 << (bits - 1)) <= result < (1 << (bits - 1))
        elif name in ('AND', 'OR', 'XOR', 'BIC'):
            result = {'AND': lambda: b & a, 'OR': lambda: b | a, 'XOR': lambda: b ^ a, 'BIC': lambda: b & ~a}[name]()
        elif name in ('SHL', 'SHR', 'SAR', 'ROL', 'ROR'):
            count = a % bits if name in ('ROL', 'ROR') else a
            if count == 0:
                result, carry = b, bool(self.psr & C)
            elif name == 'SHL':
                result = (b << count) & mask if count <= bits else 0
                carry = bool((b >> (bits - count)) & 1) if count <= bits else False
            elif name in ('SHR', 'SAR'):
                result = (sb if name == 'SAR' else b) >> min(count, bits)
                carry = bool((b >> (count - 1)) & 1) if count <= bits else name == 'SAR' and sb < 0
            elif name == 'ROL':
                result = ((b << count) | (b >> (bits - count))) & mask
                carry = bool(result & 1)
            else:
                result = ((b >> count) | (b << (bits - count))) & mask
                carry = bool(result >> (bits - 1))
        elif name in ('MIN', 'MAX'):
            result = min(sa, sb) if name == 'MIN' else max(sa, sb)
        else:
            raise Fault(1, f'unimplemented arithmetic {name}')
        self.flags(result, size, overflow, carry)
        return result & mask

    def condition(self, cond):
        c, v, z, n = (bool(self.psr & bit) for bit in (C, V, Z, N))
        return {'EQ': z, 'NE': not z, 'LT': n != v, 'LE': z or n != v,
                'GT': not z and n == v, 'GE': n == v, 'LO': c, 'LS': c or z,
                'HI': not c and not z, 'HS': not c, 'MI': n, 'PL': not n,
                'VS': v, 'VC': not v, 'BRA': True, 'BSR': True}[cond]

    def privileged(self, minimum=0):
        if self.mode > minimum:
            raise Fault(4, 'privileged instruction')

    def change_mode(self, psr):
        mode = (psr >> 12) & 3
        if mode == 3:
            raise Fault(3, 'reserved processor mode')
        old = self.mode
        if old != mode:
            self.sys[('SPK', 'SPS', 'SPU')[old]] = self.r[31]
            self.r[31] = self.sys[('SPK', 'SPS', 'SPU')[mode]]
        self.psr = psr & PSR_MASK

    def interrupt(self, vector, level):
        if not 64 <= vector <= 255 or not 1 <= level <= 7:
            raise ValueError('interrupt requires vector 64..255 and priority 1..7')
        self.interrupts.append((level, vector))

    def trap(self, fault, saved_pc, level=None):
        state = self.snapshot()
        self._writes = {}
        self._io_actions, self._rx_reads = [], 0
        try:
            self.enter_trap(fault, saved_pc, level)
            self.commit()
        except Fault:
            self.restore(state)
            self._writes = None
            self._io_actions, self._rx_reads = [], 0
            raise Fault(14, f'fault during delivery of vector {fault.vector}', saved_pc)

    def enter_trap(self, fault, saved_pc, level=None):
        old = self.psr
        self.change_mode((old & ~0xF000) | (self.mode << 14))
        if level is not None:
            self.psr = (self.psr & ~0x700) | (level << 8)
        self.sys['CAUSE'], self.sys['FAULTADDR'], self.sys['FAULTINFO'] = fault.vector, fault.address, fault.info
        handler = self.read(self.sys['VBR'] + fault.vector * 8, 8)
        self.push(saved_pc)
        self.push(old)
        self.pc = handler
        self.waiting = False

    def commit(self):
        for address, value in self._writes.items():
            self.memory.data[address] = value
        self._writes = None
        actions, self._io_actions = self._io_actions, []
        self._rx_reads = 0
        for kind, value in actions:
            if kind == 'rx':
                self.memory.serial.receive()
            else:
                self.memory.serial.transmit(value)

    def step(self):
        if self.halted:
            return None
        eligible = [item for item in self.interrupts if item[0] > ((self.psr >> 8) & 7)]
        if eligible:
            item = max(eligible)
            self.trap(Fault(item[1], 'interrupt'), self.pc, item[0])
            self.interrupts.remove(item)
            return None
        if self.waiting:
            return None
        state = self.snapshot()
        start = self.pc
        self._writes = {}
        self._io_actions, self._rx_reads = [], 0
        try:
            ins = decode(lambda address: self.read(address, 1, 4), start)
            self.pc = ins.end
            self.execute(ins)
            self.commit()
        except Fault as fault:
            self.restore(state)
            self._writes = None
            self._io_actions, self._rx_reads = [], 0
            self.last_fault = fault
            if not self.handle_traps:
                raise
            self.trap(fault, start)
            return None
        self.steps += 1
        self.sys['CYCLES'] = (self.sys['CYCLES'] + 1) & MASK64
        if self.sys['PERFCTL'] & 1:
            self.sys['PERFCNT'] = (self.sys['PERFCNT'] + 1) & MASK64
        return ins

    def run(self, max_steps=1000000, trace=None):
        for _ in range(max_steps):
            if self.halted or self.waiting and not any(level > ((self.psr >> 8) & 7) for level, _ in self.interrupts):
                return self.steps
            ins = self.step()
            if trace and ins:
                trace(ins, self)
        if not self.halted and not self.waiting:
            raise RuntimeError(f'execution limit of {max_steps} steps reached at 0x{self.pc:x}')
        return self.steps

    def execute(self, ins):
        name, size = ins.name, ins.size
        refs, values = [], []
        # Destinations and address operands must be resolved without reading data.
        no_read = {
            'MOV': {1}, 'NEG': {1}, 'NOT': {1}, 'ABS': {1}, 'CLR': {0},
            'LEA': {0, 1}, 'POP': {0}, 'JMP': {0}, 'JSR': {0},
            'FMOV': {1}, 'FNEG': {1}, 'FABS': {1},
            'MOVZ': {1}, 'MOVSX': {1}, 'ITOF': {1}, 'FTOI': {1},
            'CLZ': {1}, 'CTZ': {1}, 'POPCNT': {1}, 'BSWAP': {1},
            'MFFS': {0}, 'MFSR': {1}, 'MULW': {2, 3}, 'DIVW': {2, 3},
            'EXTV': {2, 3}, 'EXTZV': {2, 3}, 'INSV': {3}, 'CMPV': {2},
            'BBS': {1}, 'BBC': {1}, 'BBSS': {1},
            'CAS': {2}, 'XADD': {1}, 'SWAP': {1},
            'DCLEAN': {0}, 'DINVAL': {0}, 'DFLUSH': {0}, 'TLBI': {1},
        }.get(name, set())
        if name.endswith('3') and (name[:-1] in THREE or name in FP_OPS):
            no_read = {2}
        if name in CONVERSIONS or name in ('FCLASS','FSQRT','FRINT','FFLOOR','FCEIL','FTRUNC','BREV'):
            no_read = {1}
        if name in ('FMA','FMS','FNMA','FNMS'):
            no_read = {3}
        if name in ('MULW','UMULW'):
            no_read = {2,3}
        if name in ('ATAND','ATOR','ATXOR','BBCC','BBSSI','BBCCI'):
            no_read = {1}
        if name in ('MFSR', 'MTSR', 'HALT', 'WAIT', 'RTE', 'TLBI'):
            # MFSR/MTSR have per-register checks below.
            if name not in ('MFSR', 'MTSR'):
                self.privileged()
        if name in ('DCLEAN', 'DINVAL', 'DFLUSH'):
            self.privileged(1)
        for i, op in enumerate(ins.operands):
            ref = self.resolve(op, operand_size(name, size, i, ins.aux), ins.end, is_float(name, i))
            refs.append(ref)
            values.append(None if i in no_read else self.value(ref))
        if name in BRANCHES:
            if self.condition(CONDITIONS[BRANCHES.index(name)]):
                if name == 'BSR':
                    self.push(ins.end)
                self.pc = ins.target
        elif name == 'MOV':
            self.store(refs[1], values[0])
            self.flags(values[0], size)
        elif name == 'LEA':
            self.require_memory(refs[0])
            self.store(refs[1], refs[0].value)
        elif name in ('TST', 'CLR', 'NOT', 'NEG', 'ABS', 'INC', 'DEC'):
            value = 0 if name == 'CLR' else values[0]
            dest = refs[-1]
            if name == 'NOT':
                result = ~value
                self.flags(result, size)
            elif name == 'NEG':
                result = self.arithmetic('SUB', value, 0, size)
            elif name == 'ABS':
                result = abs(signed(value, size))
                self.flags(result, size, result >= 1 << (size * 8 - 1))
            elif name in ('INC', 'DEC'):
                result = self.arithmetic('ADD' if name == 'INC' else 'SUB', 1, value, size)
            else:
                result = value
                self.flags(result, size)
            if name != 'TST':
                self.store(dest, result)
        elif name == 'XCHG':
            self.store(refs[0], values[1])
            self.store(refs[1], values[0])
        elif name == 'PUSH':
            self.push(values[0], size)
        elif name == 'POP':
            self.store(refs[0], self.pop(size))
        elif name in TWO or name.endswith('3') and name[:-1] in THREE:
            op = name[:-1] if name.endswith('3') else name
            result = self.arithmetic(op, values[0], values[1], size)
            if name != 'CMP':
                self.store(refs[-1], result)
        elif name in ('NOP', 'FENCE', 'IFENCE', 'TLBI', 'DCLEAN', 'DINVAL', 'DFLUSH'):
            if name in ('DCLEAN', 'DINVAL', 'DFLUSH'):
                self.require_memory(refs[0])
            if name == 'TLBI':
                if values[0] > 3:
                    raise Fault(3,'TLBI kind must be 0..3')
                if values[0] < 2:
                    if refs[1].kind != 'imm' or self.raw(refs[1]) != 0:
                        raise Fault(3,'TLBI all/ASID forms require #0 address')
                else:
                    self.require_memory(refs[1])
        elif name in ('JMP', 'JSR'):
            self.require_memory(refs[0])
            if name == 'JSR':
                self.push(ins.end)
            self.pc = refs[0].value
        elif name == 'RET':
            self.pc = self.pop()
        elif name in ('ENTER', 'EXIT', 'PUSHM', 'POPM'):
            self.frame(name, values)
        elif name == 'RTE':
            psr, pc = self.pop(), self.pop()
            self.change_mode(psr)
            self.pc = pc
        elif name in ('BREAK', 'SYSCALL'):
            fault = Fault(11 if name == 'BREAK' else 12, name.lower(), info=values[0] if values else 0)
            if not self.handle_traps:
                raise fault
            # Entry is part of this transaction, including stack and page bits.
            try:
                self.enter_trap(fault, ins.end)
            except Fault as exc:
                raise Fault(14, f'fault during delivery of vector {fault.vector}', ins.start) from exc
        elif name == 'HALT':
            self.halted = True
        elif name == 'WAIT':
            self.waiting = True
        elif name in ('MFSR', 'MTSR', 'MFFS', 'MTFS'):
            self.system(name, refs, values)
        elif name in CONVERSIONS:
            self.convert(ins, refs, values)
        elif name in EXT and EXT[name][0] == 0xF0:
            self.floating(name, refs, values)
        elif name in ('MOVS', 'FILL', 'CMPS', 'SCAN', 'SCANN'):
            self.block(ins, values)
        elif name in ('CAS', 'XADD', 'SWAP', 'ATAND', 'ATOR', 'ATXOR'):
            self.atomic(name, refs, values, size)
        elif name in ('CLZ', 'CTZ', 'POPCNT', 'BSWAP', 'BREV'):
            value, bits = values[0], size * 8
            result = {'CLZ': lambda: bits - value.bit_length(),
                      'CTZ': lambda: (value & -value).bit_length() - 1 if value else bits,
                      'POPCNT': lambda: value.bit_count(),
                      'BSWAP': lambda: int.from_bytes(value.to_bytes(size, 'little'), 'big'),
                      'BREV': lambda: int(f'{value:0{bits}b}'[::-1], 2)}[name]()
            self.store(refs[1], result)
            self.flags(result, size)
        elif name in ('MULW','UMULW','DIVW','UDIVW'):
            self.wide(ins, refs, values)
        elif name in ('EXTV','EXTZV','INSV','CMPV') or name in BIT_BRANCHES:
            self.bitfield(ins, refs, values)
        elif name in LOOPS:
            if name.startswith('SOB'):
                if refs[0].kind != 'reg':
                    raise Fault(3, 'loop counter must be a register')
                result = self.arithmetic('SUB', 1, values[0], 8)
                self.store(refs[0], result)
                take = result != 0 if name == 'SOB' else signed(result,8) >= 0 if name == 'SOBGE' else signed(result,8) > 0
            else:
                result = self.arithmetic('ADD', values[1] if name == 'ACB' else 1, values[-1], 8)
                self.store(refs[-1], result)
                self.arithmetic('CMP', values[0], result, 8)
                take = signed(result,8) < signed(values[0],8) if name == 'AOBLT' else signed(result,8) <= signed(values[0],8)
                if name == 'ACB' and signed(values[1],8) < 0:
                    take = signed(result,8) >= signed(values[0],8)
            if take:
                self.pc = ins.target
        elif name == 'CASE':
            if refs[2].kind != 'imm':
                raise Fault(3, 'CASE limit must be a literal or immediate')
            index = (values[0] - values[1]) & MASK64
            if 0 <= index <= values[2]:
                self.pc = (ins.end + signed(self.read(ins.end + index * 4, 4), 4)) & MASK64
            else:
                self.pc = (ins.end + (values[2] + 1) * 4) & MASK64
        else:
            raise Fault(1, f'unimplemented instruction {name}')

    @staticmethod
    def require_memory(ref):
        if ref.kind != 'mem':
            raise Fault(3, 'operand requires a memory address')

    def frame(self, name, values):
        if name == 'EXIT':
            self.r[31] = self.r[30]
            old_fp, mask = self.pop(), self.pop()
            if mask > 0xFFFF:
                raise Fault(3, 'invalid saved register mask')
            restored = [(i, self.pop()) for i in range(16, 32) if mask & (1 << (i - 16))]
            return_pc, final_sp = self.pop(), self.r[31]
            for i, value in restored:
                self.r[i] = value
            self.r[30], self.r[31], self.pc = old_fp, final_sp, return_pc
            return
        mask = values[0]
        if mask > 0xFFFF:
            raise Fault(3, 'register mask exceeds 16 bits')
        saved = self.r[:]
        if name in ('ENTER', 'PUSHM'):
            for i in range(31, 15, -1):
                if mask & (1 << (i - 16)):
                    self.push(saved[i])
            if name == 'ENTER':
                self.push(mask)
                self.push(saved[30])
                self.r[30] = self.r[31]
                self.r[31] = (self.r[31] - values[1]) & MASK64
        else:
            restored = [(i, self.pop()) for i in range(16, 32) if mask & (1 << (i - 16))]
            for i, value in restored:
                self.r[i] = value

    def system(self, name, refs, values):
        if name == 'MFFS':
            self.store(refs[0], self.fpsr)
        elif name == 'MTFS':
            self.fpsr = values[0] & 0x31F1F
        else:
            number = values[0] if name == 'MFSR' else values[1]
            if number not in SYS_NAMES:
                raise Fault(3, 'unknown system register')
            register = SYS_NAMES[number]
            if refs[0 if name == 'MFSR' else 1].kind != 'imm':
                raise Fault(3, 'system register ID must be literal or immediate')
            self.privileged(1 if number <= 4 else 0)
            if name == 'MFSR':
                value = self.psr if register == 'PSR' else self.r[31] if register == ('SPK', 'SPS', 'SPU')[self.mode] else self.sys[register]
                self.store(refs[1], value)
            else:
                if register in ('ID', 'CYCLES'):
                    raise Fault(3, f'{register} is read-only')
                value = values[0]
                if register == 'ASID':
                    value &= 0xFFFF
                if register == 'MMUCTL':
                    value &= 1
                if register == 'PSR':
                    self.change_mode(value)
                else:
                    self.sys[register] = value
                if register == ('SPK', 'SPS', 'SPU')[self.mode]:
                    self.r[31] = value

    def fp_exception(self, flag):
        self.fpsr |= flag
        if self.fpsr & (flag << 8):
            raise Fault(10, 'floating-point exception', info=flag)

    def store_float(self, ref, value):
        bits, exceptions = quantize(value, ref.size, (self.fpsr >> 16) & 3)
        if exceptions:
            self.fp_exception(exceptions)
        self.store(ref, bits, raw=True)

    def block(self, ins, values):
        name, size, count = ins.name, ins.size, self.r[0]
        if count == 0:
            if name in ('SCAN', 'SCANN', 'CMPS'):
                self.psr = (self.psr & ~15) | (Z if name == 'CMPS' else 0)
            return
        source, dest = self.r[1], self.r[2]
        if name == 'MOVS':
            key = ins.start
            progress = self._blocks.get(key)
            if progress and progress[:3] != (count,source,dest):
                self._blocks.pop(key)
                progress = None
            if progress is None and source < dest < source + count * size:
                progress = (count,source,dest,(source+count*size)&MASK64,(dest+count*size)&MASK64)
            if progress:
                self.write(dest+(count-1)*size, self.read(source+(count-1)*size,size),size)
                if count == 1:
                    self.r[1], self.r[2] = progress[3:]
                    self._blocks.pop(key,None)
                else:
                    self._blocks[key] = (count-1,source,dest,*progress[3:])
            else:
                self.write(dest,self.read(source,size),size)
                self.r[1],self.r[2] = (source+size)&MASK64,(dest+size)&MASK64
        elif name == 'FILL':
            self.write(dest, values[0], size)
            self.r[2] = (dest + size) & MASK64
        elif name == 'CMPS':
            a, b = self.read(source, size), self.read(dest, size)
            self.arithmetic('CMP', a, b, size)
            if a != b:
                return
            self.r[1], self.r[2] = (source + size) & MASK64, (dest + size) & MASK64
        else:
            value = self.read(source, size)
            found = (value != values[0]) if name == 'SCANN' else (value == values[0])
            self.psr = (self.psr & ~15) | (Z if found else 0)
            if found:
                return
            self.r[1] = (source + size) & MASK64
        self.r[0] = count - 1
        if self.r[0]:
            self.pc = ins.start

    def atomic(self, name, refs, values, size):
        address_ref = refs[-1]
        self.require_memory(address_ref)
        if address_ref.value % size:
            raise Fault(3, 'unaligned atomic operand', address_ref.value)
        old = self.read(address_ref.value, size)
        if name == 'CAS':
            if old == values[0]:
                self.store(address_ref, values[1])
                self.psr |= Z
            else:
                self.store(refs[0], old)
                self.psr &= ~Z
        elif name in ('ATAND','ATOR','ATXOR'):
            old_psr = self.psr
            result = self.arithmetic({'ATAND':'AND','ATOR':'OR','ATXOR':'XOR'}[name],values[0],old,size)
            self.psr = old_psr
            self.store(address_ref,result)
            self.store(refs[0],old)
        elif name == 'SWAP':
            self.store(address_ref, values[0])
            self.store(refs[0], old)
        else:
            self.store(address_ref, self.arithmetic('ADD', values[0], old, size))
            self.store(refs[0], old)

    def field_read(self, base, position, width):
        if not 1 <= width <= 64:
            raise Fault(3, 'bitfield width must be 1..64')
        if base.kind == 'reg':
            position %= 64
            if position + width > 64:
                raise Fault(3, 'register bitfield exceeds register')
            return (self.r[base.value] >> position) & ((1 << width) - 1)
        self.require_memory(base)
        byte, bit = divmod(position, 8)
        return (self.read((base.value + byte) & MASK64, (bit + width + 7) // 8) >> bit) & ((1 << width) - 1)

    def field_write(self, base, position, width, value):
        self.field_read(base, position, width)
        mask = (1 << width) - 1
        if base.kind == 'reg':
            position %= 64
            self.r[base.value] = (self.r[base.value] & ~(mask << position)) | ((value & mask) << position)
        else:
            byte, bit = divmod(position, 8)
            address, count = (base.value + byte) & MASK64, (bit + width + 7) // 8
            old = self.read(address, count)
            self.write(address, (old & ~(mask << bit)) | ((value & mask) << bit), count)

    def bitfield(self, ins, refs, values):
        name = ins.name
        if name in BIT_BRANCHES:
            position = signed(values[0], 8)
            base = refs[1]
            if base.kind == 'reg':
                position %= 64
            if name in ('BBSSI','BBCCI'):
                self.require_memory(base)
                address = (base.value + position // 8) & MASK64
                base = Reference('mem', address & ~7,8)
                position = (address & 7)*8 + position % 8
                self.read(base.value,8)
            bit = self.field_read(base, position, 1)
            if name in ('BBSS','BBSSI','BBCC','BBCCI'):
                self.field_write(base, position, 1, int(name in ('BBSS','BBSSI')))
            if bool(bit) == (name in ('BBS','BBSS','BBSSI')):
                self.pc = ins.target
            return
        if name == 'INSV':
            self.field_write(refs[3], signed(values[1], 8), values[2], values[0])
            return
        position, width = signed(values[0], 8), values[1]
        field = self.field_read(refs[2], position, width)
        if name in ('EXTV', 'CMPV') and field & (1 << (width - 1)):
            field -= 1 << width
        if name == 'CMPV':
            self.arithmetic('CMP', field & MASK64, values[3], 8)
        else:
            self.store(refs[3], field)
            self.flags(field, ins.size)

    def wide(self, ins, refs, values):
        name, size = ins.name, ins.size
        bits, mask = size*8,(1 << (size*8))-1
        if name in ('MULW','UMULW'):
            a,b = values[:2]
            if name == 'MULW':
                a,b = signed(a,size),signed(b,size)
            result = a*b
            self.store(refs[2],result & mask)
            self.store(refs[3],(result >> bits)&mask)
            self.psr = (self.psr & ~15) | (Z if result==0 else 0) | (N if result & (1 << (2*bits-1)) else 0)
            return
        if refs[1].kind != 'reg' or refs[1].value > 30:
            raise Fault(3,'wide divide requires R0..R30')
        low_reg = refs[1].value
        dividend = ((self.r[low_reg+1]&mask) << bits) | values[1]
        divisor = values[0]
        if name == 'DIVW':
            if dividend & (1 << (2*bits-1)):
                dividend -= 1 << (2*bits)
            divisor = signed(divisor,size)
        if divisor == 0:
            raise Fault(9,'wide division by zero')
        quotient = abs(dividend)//abs(divisor)*(-1 if (dividend<0)!=(divisor<0) else 1)
        remainder = dividend-quotient*divisor
        lo,hi = (-(1 << (bits-1)),(1 << (bits-1))-1) if name=='DIVW' else (0,mask)
        if not lo <= quotient <= hi:
            raise Fault(9,'wide quotient overflow')
        self.store(refs[1],quotient)
        self.store(Reference('reg',low_reg+1,size),remainder)
