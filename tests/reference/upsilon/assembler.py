"""Safe expression parsing, label relaxation, and variable-length encoding."""
import ast
import math
import operator
import re
import struct
from dataclasses import dataclass
from .isa import *

@dataclass
class Program:
    code: bytes
    origin: int
    entry: int
    symbols: dict

def split_args(text):
    result, start, depth, quote, escape = [], 0, 0, None, False
    for i, ch in enumerate(text):
        if quote:
            if escape:
                escape = False
            elif ch == '\\':
                escape = True
            elif ch == quote:
                quote = None
        elif ch in '\"\'':
            quote = ch
        elif ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif ch == ',' and depth == 0:
            result.append(text[start:i].strip())
            start = i + 1
    if depth or quote:
        raise AssemblyError('unbalanced parentheses, brackets, or quotes')
    if text[start:].strip():
        result.append(text[start:].strip())
    return result

def uncomment(line):
    quote, escape = None, False
    for i, ch in enumerate(line):
        if escape:
            escape = False
        elif quote and ch == '\\':
            escape = True
        elif ch == quote:
            quote = None
        elif quote is None and ch in '\"\'':
            quote = ch
        elif quote is None and ch == ';':
            return line[:i]
    return line

BINOPS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
          ast.FloorDiv: operator.floordiv, ast.Mod: operator.mod,
          ast.LShift: operator.lshift, ast.RShift: operator.rshift,
          ast.BitOr: operator.or_, ast.BitAnd: operator.and_, ast.BitXor: operator.xor}

def expression(text, symbols, pc, unresolved=False):
    # The location counter is syntax, while a quoted '.' is character data.
    text = re.sub(r'''("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(?<![\w.])\.(?![\w.])''',
                  lambda match: match[1] if match[1] is not None else '__pc', text.strip())
    def visit(node):
        if isinstance(node, ast.Constant) and type(node.value) in (int, float):
            return node.value
        if isinstance(node, ast.Constant) and isinstance(node.value, str) and len(node.value) == 1:
            return ord(node.value)
        if isinstance(node, ast.Name):
            if node.id == '__pc':
                return pc
            if node.id in symbols:
                return symbols[node.id]
            if unresolved:
                return 0
            raise AssemblyError(f'undefined symbol {node.id}')
        if isinstance(node, ast.BinOp) and type(node.op) in BINOPS:
            left, right = visit(node.left), visit(node.right)
            if isinstance(node.op, (ast.LShift, ast.RShift)) and not 0 <= right <= 128:
                raise AssemblyError('shift in expression must be between 0 and 128')
            return BINOPS[type(node.op)](left, right)
        if isinstance(node, ast.UnaryOp):
            fn = {ast.UAdd: operator.pos, ast.USub: operator.neg, ast.Invert: operator.invert}.get(type(node.op))
            if fn:
                return fn(visit(node.operand))
        raise AssemblyError('unsupported expression')
    try:
        return visit(ast.parse(text, mode='eval').body)
    except (SyntaxError, TypeError, ZeroDivisionError, OverflowError) as exc:
        raise AssemblyError(f'invalid expression {text!r}: {exc}') from exc

def packed(value, width, signed_only=False):
    if not isinstance(value, int):
        raise AssemblyError('integer expression required')
    lo = -(1 << (width * 8 - 1))
    hi = (1 << (width * 8 - (1 if signed_only else 0))) - 1
    if not lo <= value <= hi:
        raise AssemblyError(f'{value} does not fit in {width * 8} bits')
    return (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little')

def register(text, floating=False):
    text = text.strip().upper()
    if text in ('SP', 'FP'):
        text = {'SP': 'R31', 'FP': 'R30'}[text]
    match = re.fullmatch(r'([RF])(\d+)', text)
    if match:
        if int(match[2]) > 31 or (match[1] == 'F') != floating:
            raise AssemblyError(f'invalid {"floating" if floating else "integer"} register {text}')
        return int(match[2])
    return None

def assemble(source, origin=0x1000, *, max_output=None):
    if not 0 <= origin <= MASK64:
        raise AssemblyError('origin outside the 64-bit address space')
    if max_output is not None and (not isinstance(max_output, int) or max_output < 0):
        raise AssemblyError('output limit must be a nonnegative integer')
    lines = [(i, uncomment(line).strip()) for i, line in enumerate(source.splitlines(), 1)]
    previous, symbols = None, {}
    for iteration in range(128):
        code, defined, entry_expr = bytearray(), set(), None
        current = dict(symbols)
        unresolved = iteration == 0
        pc = origin
        for line_no, original in lines:
            line = original
            try:
                while (match := re.match(r'^([A-Za-z_]\w*):', line)):
                    name = match[1]
                    if name in defined:
                        raise AssemblyError(f'duplicate symbol {name}')
                    current[name] = pc
                    defined.add(name)
                    line = line[match.end():].strip()
                if not line:
                    continue
                parts = line.split(None, 1)
                word, rest = parts[0], parts[1] if len(parts) > 1 else ''
                args = split_args(rest)
                ev = lambda x: expression(x, current, pc, unresolved)
                data = b''
                if word.startswith('.'):
                    directive = word.lower()
                    if directive in ('.text', '.data'):
                        if args:
                            raise AssemblyError(f'{word} takes no arguments')
                    elif directive == '.equ':
                        if len(args) != 2 or not re.fullmatch(r'[A-Za-z_]\w*', args[0]):
                            raise AssemblyError('.equ requires name, expression')
                        if args[0] in defined:
                            raise AssemblyError(f'duplicate symbol {args[0]}')
                        current[args[0]] = ev(args[1])
                        defined.add(args[0])
                    elif directive == '.entry':
                        if len(args) != 1:
                            raise AssemblyError('.entry requires one expression')
                        entry_expr = (args[0], pc)
                    elif directive in ('.byte', '.word', '.long', '.quad'):
                        width = {'.byte': 1, '.word': 2, '.long': 4, '.quad': 8}[directive]
                        data = b''.join(packed(ev(arg), width) for arg in args)
                    elif directive in ('.ascii', '.asciz'):
                        for arg in args:
                            value = ast.literal_eval(arg)
                            if not isinstance(value, str):
                                raise AssemblyError('string required')
                            data += value.encode('utf-8') + (b'\0' if directive == '.asciz' else b'')
                    elif directive in ('.org', '.align', '.space'):
                        if not 1 <= len(args) <= 2:
                            raise AssemblyError(f'{word} requires value[, fill]')
                        count = ev(args[0])
                        if directive == '.org':
                            count -= pc
                        elif directive == '.align':
                            if not isinstance(count, int) or count <= 0:
                                raise AssemblyError('alignment must be positive')
                            count = (-pc) % count
                        if not isinstance(count, int) or not 0 <= count <= 16 * 1024 * 1024:
                            raise AssemblyError('padding must be 0..16 MiB; .org cannot go backwards')
                        if max_output is not None and len(code) + count > max_output:
                            raise AssemblyError(f'output exceeds {max_output}-byte RAM file capacity')
                        data = packed(ev(args[1]) if len(args) == 2 else 0, 1) * count
                    else:
                        raise AssemblyError(f'unknown directive {word}')
                else:
                    data = encode_instruction(word, args, pc, ev, first_pass=unresolved)
                if max_output is not None and len(code) + len(data) > max_output:
                    raise AssemblyError(f'output exceeds {max_output}-byte RAM file capacity')
                code.extend(data)
                pc += len(data)
                if pc > MASK64:
                    raise AssemblyError('program crosses the end of the address space')
            except (AssemblyError, ValueError, IndexError, struct.error) as exc:
                raise AssemblyError(f'line {line_no}: {exc}\n  {original}') from exc
        entry = expression(entry_expr[0], current, entry_expr[1], unresolved) if entry_expr else origin
        if not isinstance(entry, int) or not 0 <= entry <= MASK64:
            raise AssemblyError('entry must be a 64-bit integer address')
        state = (bytes(code), current, entry)
        if state == previous and iteration > 0:
            # Resolve once without placeholders; catches forward undefined names.
            if not unresolved:
                return Program(bytes(code), origin, entry, current)
        previous, symbols = state, current
    raise AssemblyError('label relaxation did not converge after 128 passes')

def encode_instruction(word, args, pc, ev, first_pass=False):
    pieces = word.upper().split('.')
    name, suffix = ALIASES.get(pieces[0], pieces[0]), pieces[1:]
    aux = 0
    if name in CONVERSIONS:
        if len(suffix) != 2:
            raise AssemblyError(f'{name} requires source and destination sizes')
        size = (FLOAT_SIZES if is_float(name, 0) else SIZES).get(suffix[0].lower())
        aux = (FLOAT_SIZES if is_float(name, 1) else SIZES).get(suffix[1].lower())
        if size is None or aux is None:
            raise AssemblyError('invalid conversion sizes; optional binary128 is not implemented')
        if name in ('MOVIF','MOVFI') and size != aux:
            raise AssemblyError('raw moves require equal source and destination widths')
    else:
        sizes = FLOAT_SIZES if name in FP_OPS else SIZES
        if len(suffix) > 1 or suffix and suffix[0].lower() not in sizes:
            raise AssemblyError(f'invalid size suffix on {word}; binary128 is not implemented')
        size = sizes[suffix[0].lower()] if suffix else 8
    if (name == 'LEA' or name in FIXED_EXT or name in BIT_BRANCHES or name in LOOPS or name in SHORT or name in BRANCHES) and size != 8:
        raise AssemblyError(f'{name} has no size variants')
    if name == 'BSWAP' and size == 1:
        raise AssemblyError('BSWAP requires word, long or quad size')
    branch = name in BRANCHES or name in BIT_BRANCHES or name in LOOPS
    count = operand_count(name)
    if len(args) != count + int(branch):
        raise AssemblyError(f'{name} expects {count + int(branch)} operands, got {len(args)}')
    if name in BRANCHES:
        header = bytes([0xC0 + 2 * BRANCHES.index(name)])
    elif name in TWO:
        header = bytes([4 * TWO.index(name) + size.bit_length() - 1])
    elif name.endswith('3') and name[:-1] in THREE:
        header = bytes([0x80 + 4 * THREE.index(name[:-1]) + size.bit_length() - 1])
    elif name in SHORT:
        header = bytes([0xE0 + SHORT.index(name)])
    elif name in EXT:
        page, op, _ = EXT[name]
        header = bytes([page, op * 4 + size.bit_length() - (2 if page == 0xF0 else 1)])
    elif name in FIXED_EXT:
        page, op, _ = FIXED_EXT[name]
        header = bytes([page, op])
    elif name in CONVERSIONS:
        source_code = size.bit_length() - (2 if is_float(name, 0) else 1)
        dest_code = aux.bit_length() - (2 if is_float(name, 1) else 1)
        header = bytes([0xF4, CONVERSIONS.index(name) * 16 + source_code * 4 + dest_code])
    elif name in BIT_BRANCHES:
        header = bytes([0xF2, 4 + BIT_BRANCHES.index(name) * 2])
    elif name in LOOPS:
        header = bytes([0xF7, LOOPS.index(name) * 2])
    else:
        raise AssemblyError(f'unknown instruction {name}')
    data, patches = bytearray(header), []
    for i, text in enumerate(args[:count]):
        if name == 'MFSR' and i == 0 or name == 'MTSR' and i == 1:
            if text.upper() in SYS_IDS:
                text = '#' + str(SYS_IDS[text.upper()])
        operand, patch = encode_operand(text, operand_size(name, size, i, aux), is_float(name, i), ev)
        if (name in ('MFSR','MTSR') and i == (0 if name == 'MFSR' else 1) or name == 'CASE' and i == 2) and not 0xE0 <= operand[0] <= 0xFB:
            raise AssemblyError('system register number and CASE limit must be literals or immediates')
        if patch is not None:
            patches.append((len(data) + patch[0], patch[1]))
        data.extend(operand)
    width = 0
    if branch:
        distance = ev(args[-1]) - (pc + len(data) + 1)
        width = 1 if first_pass or -128 <= distance <= 127 else 4
        data[0 if name in BRANCHES else 1] |= int(width == 4)
    end = pc + len(data) + width
    for offset, target in patches:
        data[offset:offset + 4] = packed(target - end, 4, True)
    if branch:
        data.extend(packed(0 if first_pass else ev(args[-1]) - end, width, True))
    if len(data) > 48:
        raise AssemblyError('instruction exceeds the architectural 48-byte limit')
    return bytes(data)


def encode_operand(text, size, floating, ev):
    text = text.strip()
    index_match = re.fullmatch(r'(.+)\[\s*((?:R\d+)|SP|FP)\s*\]', text, re.I)
    if index_match:
        index = register(index_match[2])
        base, patch = encode_operand(index_match[1], size, floating, ev)
        mode, field = base[0] >> 5, base[0] & 31
        if mode not in (1, 4, 5, 7) or mode == 7 and field < 28:
            raise AssemblyError('index base must be a non-updating memory mode')
        return bytes([0xC0 + index]) + base, (patch[0] + 1, patch[1]) if patch else None
    reg = register(text, floating)
    if reg is not None:
        return bytes([reg]), None
    if text.startswith('#'):
        literal = text[1:].strip()
        value = float(literal) if floating and literal.lower() in ('nan', 'inf', '+inf', '-inf') else ev(literal)
        if floating:
            value = float(value)
            for index, constant in enumerate(FP_CONSTANTS):
                if (math.isnan(value) and math.isnan(constant) or value == constant and (value != 0 or math.copysign(1., value) == math.copysign(1., constant))):
                    return bytes([0xE0 + index]), None
            # Immediate width is independent of the operation's data size.
            try:
                raw32 = struct.pack('<f', value)
                if struct.unpack('<f', raw32)[0] == value or math.isnan(value):
                    return b'\xFA' + raw32, None
            except OverflowError:
                pass
            return b'\xFB' + struct.pack('<d', value), None
        if not isinstance(value, int) or not -(1 << (size * 8 - 1)) <= value < (1 << (size * 8)):
            raise AssemblyError('immediate does not fit operand size')
        if 0 <= value <= 23:
            return bytes([0xE0 + value]), None
        value = signed(value, size)
        for field, width in ((24, 1), (25, 2), (26, 4), (27, 8)):
            if -(1 << (width * 8 - 1)) <= value < (1 << (width * 8 - 1)):
                return bytes([0xE0 + field]) + packed(value, width), None
    match = re.fullmatch(r'\(\s*(R\d+|SP|FP)\s*\)(\+)?', text, re.I)
    if match:
        return bytes([(0x40 if match[2] else 0x20) + register(match[1])]), None
    match = re.fullmatch(r'-\(\s*(R\d+|SP|FP)\s*\)', text, re.I)
    if match:
        return bytes([0x60 + register(match[1])]), None
    indirect = text.startswith('[') and text.endswith(']')
    inner = text[1:-1].strip() if indirect else text
    match = re.fullmatch(r'(.*?)\(\s*(R\d+|SP|FP|PC)\s*\)', inner, re.I)
    if match:
        value = ev(match[1] or '0')
        if match[2].upper() == 'PC':
            if indirect:
                raise AssemblyError('PC-relative memory-indirect mode is not defined')
            # expr(PC) is a target expression, not a raw displacement.
            return b'\xFE\0\0\0\0', (1, value)
        reg = register(match[2])
        if indirect:
            return bytes([0xFF, 0x40 + reg]) + packed(value, 4, True), None
        if -128 <= value <= 127:
            return bytes([0x80 + reg]) + packed(value, 1), None
        if -32768 <= value <= 32767:
            return bytes([0xA0 + reg]) + packed(value, 2), None
        if -(1 << 31) <= value < (1 << 31):
            return bytes([0xFF, reg]) + packed(value, 4), None
        return bytes([0xFF, 0x20 + reg]) + packed(value, 8, True), None
    if indirect:
        raise AssemblyError('memory indirect syntax is [displacement(Rn)]')
    if text.startswith('@'):
        value = ev(text[1:])
        if -(1 << 31) <= value < (1 << 31) or 0xFFFFFFFF80000000 <= value <= MASK64:
            return b'\xFC' + packed(signed(value, 8), 4), None
        return b'\xFD' + packed(value, 8), None
    # Bare expressions (usually labels) denote PC-relative memory.
    return b'\xFE\0\0\0\0', (1, ev(text))
