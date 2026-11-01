"""Shared decoder: the simulator always executes the emitted machine bytes."""
import struct
from .isa import *

def decode(fetch, pc):
    start = pc
    def get(n=1, signed_value=False):
        nonlocal pc
        if pc + n - start > 48:
            raise Fault(1, 'instruction exceeds 48 bytes', start)
        raw = bytes(fetch(pc + i) for i in range(n))
        pc += n
        return int.from_bytes(raw, 'little', signed=signed_value)
    first = get()
    size, target, aux, branch_width = 8, None, 0, 0
    if first < 0x80:
        op, size = first >> 2, 1 << (first & 3)
        if op >= len(TWO) or op == 20 and size != 8:
            raise Fault(1, 'reserved integer opcode', start)
        name = TWO[op]
    elif first < 0xC0:
        op, size = (first - 0x80) >> 2, 1 << (first & 3)
        if op >= len(THREE):
            raise Fault(1, 'reserved three-operand opcode', start)
        name = THREE[op] + '3'
    elif first < 0xE0:
        name = BRANCHES[(first - 0xC0) >> 1]
        branch_width = 4 if first & 1 else 1
    elif first < 0xF0:
        op = first - 0xE0
        if op >= len(SHORT):
            raise Fault(1, 'reserved short opcode', start)
        name = SHORT[op]
    elif first <= 0xF7:
        second = get()
        name = None
        for candidate, (page, op, _) in FIXED_EXT.items():
            if (page, op) == (first, second):
                name = candidate
                break
        if name is None:
            for candidate, (page, op, _) in EXT.items():
                if page == first and second >> 2 == op:
                    if page == 0xF0:
                        if second & 3 == 3:
                            raise Fault(1, 'binary128 is not implemented', start)
                        size = 2 << (second & 3)
                    else:
                        size = 1 << (second & 3)
                    name = candidate
                    if name == 'BSWAP' and size == 1:
                        raise Fault(1, 'byte BSWAP is reserved', start)
                    break
        if first == 0xF4 and second >> 4 < len(CONVERSIONS):
            name = CONVERSIONS[second >> 4]
            source, dest = (second >> 2) & 3, second & 3
            if is_float(name, 0) and source == 3 or is_float(name, 1) and dest == 3:
                raise Fault(1, 'binary128 conversion is not implemented', start)
            size = 1 << (source + int(is_float(name, 0)))
            aux = 1 << (dest + int(is_float(name, 1)))
            if name in ('MOVIF','MOVFI') and size != aux:
                raise Fault(3, 'raw move width mismatch', start)
        if first == 0xF2 and 4 <= second <= 15:
            name = BIT_BRANCHES[(second - 4) >> 1]
            branch_width = 4 if second & 1 else 1
        if first == 0xF7 and second < 12:
            name = LOOPS[second >> 1]
            branch_width = 4 if second & 1 else 1
        if name is None:
            raise Fault(1, 'reserved extension opcode', start)
    else:
        raise Fault(1, 'reserved escape page', start)

    def operand(indexed=False):
        spec = get()
        mode, reg = spec >> 5, spec & 31
        if mode == 6:
            if indexed:
                raise Fault(2, 'nested index prefix', start)
            base = operand(True)
            if base.mode not in (1, 4, 5, 7) or base.mode == 7 and not base.kind and base.reg < 28:
                raise Fault(2, 'invalid indexed base', start)
            return Operand(base.mode, base.reg, base.value, reg, base.kind)
        if mode in (4, 5):
            return Operand(mode, reg, get(1 if mode == 4 else 2, True))
        if mode == 7 and reg >= 24:
            if reg <= 27:
                width = (1, 2, 4, 8)[reg - 24]
                return Operand(mode, reg, get(width, True))
            if reg in (28, 29, 30):
                return Operand(mode, reg, get(8 if reg == 29 else 4, reg != 29))
            long = get()
            kind, base = long >> 5, long & 31
            if kind > 2:
                raise Fault(2, 'reserved long addressing kind', start)
            return Operand(mode, base, get(8 if kind == 1 else 4, True), kind=kind + 1)
        return Operand(mode, reg)
    operands = tuple(operand() for _ in range(operand_count(name)))
    if branch_width:
        displacement = get(branch_width, True)
        target = (pc + displacement) & MASK64
    return Instruction(name, size, operands, start, pc & MASK64, target, aux)

def format_operand(op, size, end, floating=False):
    mode, reg, value = op.mode, op.reg, op.value
    if mode == 0:
        result = ('F' if floating else 'R') + str(reg)
    elif mode == 1:
        result = f'(R{reg})'
    elif mode == 2:
        result = f'(R{reg})+'
    elif mode == 3:
        result = f'-(R{reg})'
    elif mode in (4, 5):
        result = f'{value}(R{reg})'
    elif op.kind:
        result = f'{value}(R{reg})'
        if op.kind == 3:
            result = '[' + result + ']'
    elif reg <= 27:
        if floating:
            if reg < 24:
                value = FP_CONSTANTS[reg]
            elif reg in (26, 27):
                width = 4 if reg == 26 else 8
                value = struct.unpack('<f' if width == 4 else '<d', (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little'))[0]
        result = '#' + repr(value if reg >= 24 else FP_CONSTANTS[reg] if floating else reg)
    elif reg in (28, 29):
        result = f'@0x{value & MASK64:x}'
    else:
        result = f'0x{(end + value) & MASK64:x}(PC)'
    return f'{result}[R{op.index}]' if op.index is not None else result

def format_instruction(ins):
    name = ins.name
    if name in CONVERSIONS:
        a = FLOAT_NAMES if is_float(name, 0) else SIZE_NAMES
        b = FLOAT_NAMES if is_float(name, 1) else SIZE_NAMES
        name += '.' + a[ins.size] + '.' + b[ins.aux]
    elif name in TWO or name.endswith('3') and name[:-1] in THREE or name in EXT:
        name += '.' + (FLOAT_NAMES if is_float(ins.name, 0) else SIZE_NAMES)[ins.size]
    args = [format_operand(op, operand_size(ins.name, ins.size, i, ins.aux), ins.end, is_float(ins.name, i)) for i, op in enumerate(ins.operands)]
    if ins.target is not None:
        args.append(f'0x{ins.target:x}')
    return name + (' ' + ', '.join(args) if args else '')
