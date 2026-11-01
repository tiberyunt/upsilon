"""Kotl Upsilon v0.2 metadata (Appendices A and B)."""
from dataclasses import dataclass
import math

MASK64 = (1 << 64) - 1
C, V, Z, N = 1, 2, 4, 8
SIZES = {'b': 1, 'w': 2, 'l': 4, 'q': 8}
FLOAT_SIZES = {'h': 2, 's': 4, 'd': 8}
SIZE_NAMES = {v: k for k, v in SIZES.items()}
FLOAT_NAMES = {v: k for k, v in FLOAT_SIZES.items()}
TWO = 'MOV ADD SUB CMP AND OR XOR BIC MUL DIV ADC SBB SHL SHR SAR ROL ROR NEG NOT TST LEA PUSH POP XCHG INC DEC CLR ABS MIN MAX'.split()
THREE = 'ADD SUB MUL DIV AND OR XOR BIC SHL SHR SAR ROL ROR ADC SBB'.split()
CONDITIONS = 'EQ NE LT LE GT GE LO LS HI HS MI PL VS VC BRA BSR'.split()
BRANCHES = [x if x in ('BRA', 'BSR') else 'B' + x for x in CONDITIONS]
SHORT = 'NOP RET EXIT RTE IFENCE FENCE BREAK WAIT JMP JSR ENTER PUSHM POPM SYSCALL'.split()
ONE = {'TST', 'PUSH', 'POP', 'INC', 'DEC', 'CLR'}
FP_CONSTANTS = (0., 1., 2., .5, 3., 4., 5., 8., 10., 16., 100., .25, .1, -1., -2., -.5,
                math.pi, math.pi / 2, math.e, math.log(2), math.sqrt(2), math.inf, math.nan, -0.)
SYS_IDS = dict(zip('ID CYCLES PERFCTL PERFCNT SIMASK VBR PTBR0 PTBR1 ASID MMUCTL CAUSE FAULTADDR FAULTINFO SPK SPS SPU PSR SCRATCH0 SCRATCH1 SCRATCH2 SCRATCH3'.split(),
                   [0,1,2,3,4,16,17,18,19,20,21,22,23,24,25,26,27,32,33,34,35]))
SYSTEM_REGISTERS = list(SYS_IDS)
SYS_NAMES = {v: k for k, v in SYS_IDS.items()}
FP_OPS = 'FMOV FADD FSUB FMUL FDIV FCMP FCMPE FTST FNEG FABS FSQRT FMIN FMAX FCOPYSIGN FREM FCLASS FRINT FFLOOR FCEIL FTRUNC FADD3 FSUB3 FMUL3 FDIV3 FMA FMS FNMA FNMS'.split()
CONVERSIONS = 'MOVZ MOVSX CVTIF CVTUF CVTFI CVTFU CVTFIR CVTFF MOVIF MOVFI'.split()
BIT_BRANCHES = 'BBS BBC BBSS BBCC BBSSI BBCCI'.split()
LOOPS = 'SOB SOBGE SOBGT AOBLT AOBLE ACB'.split()
ALIASES = {'ITOF': 'CVTIF', 'FTOI': 'CVTFIR'}

@dataclass(frozen=True)
class Instruction:
    name: str
    size: int
    operands: tuple
    start: int
    end: int
    target: int | None = None
    aux: int = 0

@dataclass(frozen=True)
class Operand:
    mode: int
    reg: int = 0
    value: int = 0
    index: int | None = None
    kind: int = 0

class AssemblyError(ValueError):
    pass

class Fault(Exception):
    def __init__(self, vector, message, address=0, info=0):
        super().__init__(message)
        self.vector, self.address, self.info = vector, address, info

def signed(value, size):
    bits = size * 8
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value

EXT = {name: (0xF0, op, 1 if name == 'FTST' else 3 if name.endswith('3') else 4 if name in ('FMA','FMS','FNMA','FNMS') else 2)
       for op, name in enumerate(FP_OPS)}
EXT.update({name: (0xF1, op, 0 if name in ('MOVS','CMPS') else 1)
            for op, name in enumerate(('MOVS','FILL','CMPS','SCAN','SCANN'))})
EXT.update({name: (0xF3, op, 3 if name == 'CAS' else 2)
            for op, name in enumerate(('CAS','XADD','SWAP','ATAND','ATOR','ATXOR'))})
EXT.update({name: (0xF5, op, count) for name, op, count in [
    ('CLZ',0,2),('CTZ',1,2),('POPCNT',2,2),('BSWAP',3,2),('BREV',4,2),
    ('MULW',8,4),('UMULW',9,4),('DIVW',10,2),('UDIVW',11,2)]})
FIXED_EXT = {
    'MFFS': (0xF0,112,1), 'MTFS': (0xF0,116,1),
    'EXTV': (0xF2,0,4), 'EXTZV': (0xF2,1,4), 'INSV': (0xF2,2,4), 'CMPV': (0xF2,3,4),
    'MFSR': (0xF6,0,2), 'MTSR': (0xF6,1,2), 'TLBI': (0xF6,2,2),
    'DCLEAN': (0xF6,3,2), 'DINVAL': (0xF6,4,2), 'DFLUSH': (0xF6,5,2), 'HALT': (0xF6,6,0),
    'CASE': (0xF7,12,3),
}

def operand_count(name):
    if name in TWO:
        return 1 if name in ONE else 2
    if name.endswith('3') and name[:-1] in THREE:
        return 3
    if name in SHORT:
        return {'JMP':1,'JSR':1,'ENTER':2,'PUSHM':1,'POPM':1,'SYSCALL':1}.get(name,0)
    if name in EXT:
        return EXT[name][2]
    if name in FIXED_EXT:
        return FIXED_EXT[name][2]
    if name in CONVERSIONS or name in BIT_BRANCHES:
        return 2
    if name in LOOPS:
        return 1 if name.startswith('SOB') else 3 if name == 'ACB' else 2
    if name in BRANCHES:
        return 0
    raise AssemblyError(f'unknown instruction {name}')

def is_float(name, index):
    if name in FP_OPS:
        return not (name == 'FCLASS' and index == 1)
    if name in ('CVTIF','CVTUF','MOVIF'):
        return index == 1
    if name in ('CVTFI','CVTFU','CVTFIR','MOVFI'):
        return index == 0
    return name == 'CVTFF'

def operand_size(name, size, index, aux=0):
    if name in CONVERSIONS:
        return size if index == 0 else aux
    if name == 'FCLASS' and index == 1:
        return 8
    return size
