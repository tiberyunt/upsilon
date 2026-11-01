import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

from upsilon import assemble, CPU, Memory, AssemblyError, Fault
from upsilon.isa import *
from upsilon.decoder import decode, format_instruction

ROOT = Path(__file__).resolve().parents[1]

def machine(source, ram=65536, **kwargs):
    cpu = CPU(Memory(ram), **kwargs)
    program = assemble(source)
    cpu.load(program)
    return cpu, program

def run(source):
    cpu, _ = machine(source + '\nHALT')
    cpu.run(10000)
    return cpu

class AssemblerTests(unittest.TestCase):
    def test_independent_golden_encodings(self):
        cases = {
            'NOP': 'e0', 'HALT': 'f606', 'RET': 'e1',
            'ADD.l #1, (R3)+': '06e143',
            'MOV.q 8(FP), R4': '039e0804',
            'MOV.l (R1)[R2], R5': '02c22105',
            'MOV.q #1000, R0': '03f9e80300',
            'MOV.q #0x123456789abcdef0, R31': '03fbf0debc9a785634121f',
            'MOV.q @0x12345678, R2': '03fc7856341202',
            'MOV.q 65536(R1), R2': '03ff010000010002',
            'MOV.q [8(R1)], R2': '03ff410800000002',
            'ADD3.q R0, R1, R2': '83000102',
            'BRA .': 'dcfe', 'BSR .': 'defe',
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(assemble(source).code.hex(), expected)

    def test_shortest_immediates(self):
        for value, field in [(23, 23), (24, 24), (127, 24), (128, 25),
                             (32767, 25), (32768, 26), (2**31, 27), (-1, 24)]:
            self.assertEqual(assemble(f'MOV.q #{value}, R0').code[1] & 31, field)
        self.assertEqual(assemble('MOV.b #255, R0').code.hex(), '00f8ff00')
        self.assertEqual(assemble('FMOV.d #1.5, F0').code[2], 0xFA)
        self.assertEqual(assemble('FMOV.s #1e100, F0').code[2], 0xFB)

    def test_forward_labels_relax_branches(self):
        short = assemble('BRA target\n.space 127\ntarget: HALT')
        self.assertEqual(short.code[:2], b'\xdc\x7f')
        long = assemble('BRA target\n.space 128\ntarget: HALT')
        self.assertEqual(long.code[:5], b'\xdd\x80\0\0\0')
        self.assertEqual(long.symbols['target'], 0x1085)
        self.assertEqual(assemble('BBS #1, R0, next\nnext: NOP').code[:5], b'\xf2\x04\xe1\0\0')
        self.assertEqual(assemble('BBC #1, R0, next\n.space 128\nnext: NOP').code[:2], b'\xf2\x07')

    def test_pc_relative_is_from_whole_instruction_end(self):
        cpu = run('MOV.q datum, 8(R1)\nBRA done\ndatum: .quad 0xfeedface\ndone: NOP')
        self.assertEqual(cpu.memory.read(8, 8), 0xFEEDFACE)

    def test_directives_and_expressions(self):
        p = assemble('''.equ count, 3
.entry start
.byte 'A', 1 << 3, count
.ascii "a;b", "c"
.asciz "!"
.align 16, 0xcc
start: MOV.q #(count * 10 | 1), R0
.org 0x1020
.word -1
''')
        self.assertEqual(p.code[:9], b'A\x08\x03a;bc!\0')
        self.assertEqual(p.entry, 0x1010)
        self.assertEqual(p.code[-2:], b'\xff\xff')

    def test_bad_source_reports_line(self):
        cases = ['MOV.q #1, R32', 'MOV.q F0, R0', 'MOV.d R0, R1',
                 'LEA.l (R0), R1', 'MOV.q #missing, R0', 'BOGUS R0',
                 'NOP R0', 'MOV.q (R0)+[R1], R2', 'MOV.q (R0)[R1][R2], R3',
                 '.equ x, 1\nx: NOP', '.align 0', '.org 0',
                 'MOV.q #__import__("os"), R0', 'MOV.q #1.5, R0']
        for source in cases:
            with self.subTest(source=source), self.assertRaisesRegex(AssemblyError, 'line '):
                assemble(source)

    def test_round_trip_supported_instructions(self):
        sources = []
        for suffix in SIZES:
            for name in TWO:
                if name == 'LEA' and suffix != 'q':
                    continue
                args = '(R0), R1' if name == 'LEA' else 'R0' if name in ONE else 'R0, R1'
                sources.append(f'{name}.{suffix} {args}')
            sources.extend(f'{name}3.{suffix} R0, R1, R2' for name in THREE)
        sources.extend(f'{name} .' for name in BRANCHES)
        sources.extend(f'{name}.{suffix} ' + ', '.join(('F' if is_float(name,i) else 'R') + str(i) for i in range(operand_count(name))) for name in FP_OPS for suffix in FLOAT_SIZES)
        sources += ['MOV.q 0x100(R2)[R3], R4', 'MOV.q 0x10000(R2)[R3], R4',
                    'MOV.q [8(R2)][R3], R4', 'MOV.q @0x123456789abcdef0[R2], R4',
                    'MOVSX.b.q R0, R1', 'ITOF.l.d R0, F1', 'FTOI.s.w F0, R1',
                    'SOB R0, .', 'ACB R0, R1, R2, .', 'BBS #1, R2, .',
                    'CAS.q R0, R1, (R2)', 'MFSR ID, R0', 'MTSR R0, SCRATCH0']
        for source in sources:
            with self.subTest(source=source):
                program = assemble(source)
                ins = decode(lambda address: program.code[address - program.origin], program.origin)
                self.assertEqual(ins.end - ins.start, len(program.code))
                self.assertEqual(assemble(format_instruction(ins)).code, program.code)

class IntegerTests(unittest.TestCase):
    def test_narrow_register_zero_extension(self):
        cpu = run('MOV.q #0xabcdef1234567890, R0\nMOV.b #0xff, R0\nMOV.w #0x1234, R0\nMOV.l #0x76543210, R0')
        self.assertEqual(cpu.r[0], 0x76543210)

    def test_arithmetic_flags_boundaries(self):
        for suffix, size in SIZES.items():
            bits = size * 8
            for a, b, op, result, flags in [
                (1, (1 << (bits - 1)) - 1, 'ADD', 1 << (bits - 1), N | V),
                (1, (1 << bits) - 1, 'ADD', 0, C | Z),
                (1, 0, 'SUB', (1 << bits) - 1, C | N),
                (1, 1 << (bits - 1), 'SUB', (1 << (bits - 1)) - 1, V),
                (9, 9, 'CMP', 9, Z),
            ]:
                with self.subTest(size=size, a=a, b=b, op=op):
                    cpu = run(f'MOV.{suffix} #{b}, R0\n{op}.{suffix} #{a}, R0')
                    self.assertEqual(cpu.r[0], result)
                    self.assertEqual(cpu.psr & 15, flags)

    def test_random_add_sub_against_mathematical_reference(self):
        rng = random.Random(7319)
        for size in (1, 2, 4, 8):
            bits, modulus = size * 8, 1 << (size * 8)
            suffix = SIZE_NAMES[size]
            for _ in range(30):
                a, b = rng.randrange(modulus), rng.randrange(modulus)
                for op, exact in [('ADD', a + b), ('SUB', b - a)]:
                    cpu = run(f'MOV.{suffix} #{b}, R0\n{op}.{suffix} #{a}, R0')
                    self.assertEqual(cpu.r[0], exact % modulus)
                    self.assertEqual(bool(cpu.psr & C), exact >= modulus if op == 'ADD' else exact < 0)

    def test_carry_chain_and_borrow_chain(self):
        cpu = run('MOV.q #-1, R0\nADD.q #1, R0\nADC.q #0, R1\nSUB.q #1, R0\nSBB.q #0, R2')
        self.assertEqual(cpu.r[:3], [MASK64, 1, MASK64])

    def test_signed_division_and_three_operand_order(self):
        cpu = run('SUB3.q #3, #10, R0\nDIV3.q #3, #-10, R1\nMUL3.q #-2, #7, R2\nBIC3.q #3, #15, R3')
        self.assertEqual(cpu.r[:4], [7, MASK64 - 2, MASK64 - 13, 12])

    def test_shift_rotate_and_bit_utilities(self):
        cpu = run('MOV.b #0x81, R0\nROL.b #1, R0\nSHR.b #1, R0\nSAR.b #8, R0\nCLZ.q #1, R1\nCTZ.q #0, R2\nPOPCNT.q #0xf0f0, R3\nBSWAP.l #0x12345678, R4')
        self.assertEqual(cpu.r[:5], [0, 63, 64, 8, 0x78563412])
        cpu = run('MOV.b #0x80, R0\nSAR.b #255, R0')
        self.assertEqual(cpu.r[0], 255)
        self.assertTrue(cpu.psr & C)

    def test_all_conditions(self):
        cpu = CPU()
        for flags in range(16):
            cpu.psr = flags
            c, v, z, n = [bool(flags & bit) for bit in (C, V, Z, N)]
            expected = [z, not z, n != v, z or n != v, not z and n == v,
                        n == v, c, c or z, not c and not z, not c, n, not n, v, not v, True, True]
            self.assertEqual([cpu.condition(name) for name in CONDITIONS], expected)

    def test_examples(self):
        for name, register, expected in [('fibonacci', 0, 6765), ('factorial', 0, 3628800), ('memory', 5, 30)]:
            cpu, _ = machine((ROOT / 'examples' / (name + '.s')).read_text())
            initial_sp = cpu.r[31]
            cpu.run()
            self.assertEqual(cpu.r[register], expected)
            self.assertEqual(cpu.r[31], initial_sp)

class AddressAndFaultTests(unittest.TestCase):
    def test_left_to_right_autoincrement(self):
        cpu, p = machine('LEA data, R3\nADD3.q (R3)+, (R3)+, R4\nHALT\ndata: .quad 10, 20')
        cpu.run()
        self.assertEqual(cpu.r[4], 30)
        self.assertEqual(cpu.r[3], p.symbols['data'] + 16)

    def test_modes_and_unaligned_access(self):
        cpu, p = machine('''MOV.q #1001, R1
MOV.q #0x123456789abcdef0, (R1)
MOV.q (R1)+, R2
MOV.q -(R1), R3
MOV.q #20000, R4
MOV.q -18999(R4), R5
MOV.q #1000000, R6
MOV.q -998999(R6), R7
MOV.q #1001, @2000
MOV.q #2000, R8
MOV.q [0(R8)], R9
MOV.q #0, R10
MOV.q [0(R8)][R10], R11
HALT''')
        cpu.run()
        self.assertEqual(cpu.r[1], 1001)
        for index in (2, 3, 5, 7, 9, 11):
            self.assertEqual(cpu.r[index], 0x123456789ABCDEF0)

    def test_fault_rolls_back_all_state_and_memory(self):
        cpu, _ = machine('XCHG.q (R1)+, (R2)+', ram=8192)
        cpu.r[1], cpu.r[2] = 100, 8188
        cpu.memory.load(100, b'abcdefgh')
        before, memory = cpu.snapshot(), bytes(cpu.memory.data)
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 8)
        self.assertEqual(cpu.snapshot(), before)
        self.assertEqual(bytes(cpu.memory.data), memory)
        cpu.r[2] = 200
        cpu.step()
        self.assertEqual(cpu.memory.data[200:208], b'abcdefgh')
        self.assertEqual(cpu.r[1:3], [108, 208])

    def test_write_crossing_ram_end_is_atomic(self):
        cpu, _ = machine('MOV.q #123, (R0)+', ram=8192)
        cpu.r[0] = 8188
        before = bytes(cpu.memory.data)
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(cpu.r[0], 8188)
        self.assertEqual((caught.exception.address, caught.exception.info), (8192, 2))
        self.assertEqual(bytes(cpu.memory.data), before)

    def test_divide_fault_is_restartable(self):
        cpu, _ = machine('DIV.q (R0)+, R1')
        cpu.r[0], cpu.r[1] = 200, 42
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 9)
        self.assertEqual(cpu.r[:2], [200, 42])
        self.assertEqual(cpu.pc, 0x1000)

    def test_reserved_modes_and_illegal_opcodes(self):
        cases = [(b'\x03\xc0\xc1\x22\x00', 2),
                 (b'\x03\xc0\xe1\x00', 2), (b'\x03\xff\xe0', 2),
                 (b'\x78', 1), (b'\xbc', 1), (b'\xfc', 1),
                 (b'\x03\xe1\xe2', 3)]
        for code, vector in cases:
            cpu = CPU(Memory(8192))
            cpu.memory.load(0x1000, code)
            cpu.pc = 0x1000
            with self.subTest(code=code), self.assertRaises(Fault) as caught:
                cpu.step()
            self.assertEqual(caught.exception.vector, vector)

    def test_frames_save_all_masks_including_sp_fp(self):
        for mask in (0, 3, 0xC003, 0xFFFF):
            cpu, _ = machine(f'JSR func\nHALT\nfunc: ENTER #{mask}, #24\nMOV.q #99, R16\nEXIT')
            cpu.r[16], cpu.r[17], cpu.r[30] = 16, 17, 1234
            sp = cpu.r[31]
            cpu.run()
            self.assertEqual(cpu.r[30:32], [1234, sp])
            if mask & 1:
                self.assertEqual(cpu.r[16:18], [16, 17])

    def test_pushm_popm(self):
        cpu = run('MOV.q #123, R16\nMOV.q #456, R17\nPUSHM #3\nCLR.q R16\nCLR.q R17\nPOPM #3')
        self.assertEqual(cpu.r[16:18], [123, 456])
        self.assertEqual(cpu.r[31], 65536)

class ExtensionTests(unittest.TestCase):
    def test_memmove_overlap_in_both_directions(self):
        for source, dest, count in [(200, 203, 12), (203, 200, 12), (200, 300, 12), (200, 200, 12)]:
            cpu, _ = machine('MOVS.b\nHALT')
            cpu.memory.load(200, bytes(range(128)))
            expected = bytearray(cpu.memory.data)
            expected[dest:dest + count] = expected[source:source + count]
            cpu.r[:3] = count, source, dest
            cpu.run()
            self.assertEqual(cpu.memory.data, expected)
            self.assertEqual(cpu.r[0], 0)

    def test_block_fault_keeps_previous_progress(self):
        cpu, _ = machine('MOVS.q', ram=8192)
        cpu.memory.load(100, b'abcdefghijklmnop')
        cpu.r[:3] = [2, 8184, 100]
        cpu.step()
        self.assertEqual(cpu.r[:3], [1, 8192, 108])
        before = cpu.snapshot()
        with self.assertRaises(Fault):
            cpu.step()
        self.assertEqual(cpu.snapshot(), before)

    def test_fill_scan_cmps(self):
        cpu, _ = machine('FILL.w #0x1234\nHALT')
        cpu.r[:3] = [4, 0, 300]
        cpu.run()
        self.assertEqual(cpu.memory.data[300:308], b'\x34\x12' * 4)
        cpu, _ = machine('SCAN.b #42\nHALT')
        cpu.memory.load(300, b'\x01\x02\x2a\x03')
        cpu.r[:2] = [4, 300]
        cpu.run()
        self.assertEqual(cpu.r[:2], [2, 302])
        self.assertTrue(cpu.psr & Z)
        cpu, _ = machine('CMPS.b\nHALT')
        cpu.memory.load(300, b'abcabd')
        cpu.r[:3] = [3, 300, 303]
        cpu.run()
        self.assertEqual(cpu.r[:3], [1, 302, 305])
        self.assertFalse(cpu.psr & Z)

    def test_atomics(self):
        cpu = run('MOV.q #10, @256\nMOV.q #10, R0\nCAS.q R0, #20, @256\nMOV.q #5, R1\nXADD.q R1, @256\nMOV.q #99, R2\nSWAP.q R2, @256\nCAS.q R0, #42, @256')
        self.assertEqual(cpu.r[:3], [99, 20, 25])
        self.assertEqual(cpu.memory.read(256, 8), 99)
        self.assertFalse(cpu.psr & Z)
        cpu, _ = machine('XADD.q R0, @257')
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 3)

    def test_bitfields_across_bytes_and_negative_positions(self):
        cpu = run('MOV.q #0x8000, R0\nEXTV #12, #4, R0, R1\nEXTZV #12, #4, R0, R2\nINSV #0xabc, #7, #12, @300\nEXTZV #-1, #12, @301, R3\nMOV.q #0xabc, R4\nCMPV #7, #12, @300, R4')
        self.assertEqual(cpu.r[1:4], [MASK64 - 7, 8, 0xABC])
        # CMPV compares a signed field, so 0xabc means -1348.
        self.assertFalse(cpu.psr & Z)

    def test_bit_branches(self):
        cpu = run('MOV.q #2, R0\nBBS #1, R0, yes\nMOV.q #99, R1\nyes: BBSS #0, R0, bad\nBBC #2, R0, done\nbad: MOV.q #100, R1\ndone: NOP')
        self.assertEqual(cpu.r[:2], [3, 0])

    def test_extensions_wide_conversions_and_loops(self):
        cpu = run('MOVZ.b.q #255, R0\nMOVSX.b.q #255, R1\nUMULW #-1, #2, R2, R3\nMOV.q #2, R4\nMOV.q #1, R5\nUDIVW #3, R4\nACB #4, #2, R6, more\nMOV.q #99, R7\nmore: NOP')
        self.assertEqual(cpu.r[:6], [255, MASK64, MASK64 - 1, 1, (2**64 + 2) // 3, (2**64 + 2) % 3])
        self.assertEqual(cpu.r[6:8], [2, 0])

    def test_conversion_address_modes_use_independent_sizes(self):
        cpu = run('MOV.q #200, R0\nMOV.b #255, (R0)\nMOVSX.b.q (R0)+, (R0)+')
        self.assertEqual(cpu.r[0], 209)
        self.assertEqual(cpu.memory.read(201, 8), MASK64)

    def test_case_relative_table(self):
        cpu = run('CASE #11, #10, #1\ntable: .long one-table, two-table\none: MOV.q #1, R0\nBRA done\ntwo: MOV.q #2, R0\ndone: NOP')
        self.assertEqual(cpu.r[0], 2)

    def test_float_formats_operations_and_conversion(self):
        for suffix in FLOAT_SIZES:
            cpu = run(f'FMOV.{suffix} #1.5, F0\nFADD.{suffix} #2.0, F0\nFMUL.{suffix} #2.0, F0\nFTOI.{suffix}.q F0, R0\nITOF.q.{suffix} #-3, F1\nFCMP.{suffix} #0.0, F1')
            self.assertEqual(cpu.r[0], 7)
            self.assertEqual(cpu.psr & 15, N | C)

    def test_float_zero_extension_and_signed_zero(self):
        cpu, _ = machine('FMOV.s #1.0, F0\nFMOV.d #-0.0, F1\nHALT')
        cpu.f[0] = 0xABCDEF0000000000
        cpu.run()
        self.assertEqual(cpu.f[0], 0x3F800000)
        self.assertEqual(cpu.f[1], 1 << 63)
        cpu = run('FMOV.d #nan, F0\nFMOV.d #inf, F1\nFCMP.d F0, F1')
        self.assertEqual(cpu.psr & 15, V)

    def test_float_exceptions_and_rounding(self):
        cpu = run('FMOV.d #1.0, F0\nFDIV.d #0.0, F0\nMFFS R0\nFCMP.d F0, F0')
        self.assertTrue(math.isinf(cpu.bits_float(cpu.f[0], 8)))
        self.assertTrue(cpu.r[0] & (1 << 3))
        self.assertEqual(cpu.psr & 15, Z)
        cpu = run('FMOV.d #0.0, F0\nFDIV.d #0.0, F0\nFCMP.d F0, F0')
        self.assertEqual(cpu.psr & 15, V)
        cpu = run('FMOV.d #2.75, F0\nMTFS #65536\nFTOI.d.q F0, R0')
        self.assertEqual(cpu.r[0], 2)

    def test_exact_binary64_rounding_and_inexact_flag(self):
        # 1 + 2^-53 is exactly halfway between two binary64 values.
        for rounding, bits in [(0, 0x3FF0000000000000), (1, 0x3FF0000000000000),
                               (2, 0x3FF0000000000001), (3, 0x3FF0000000000000)]:
            cpu = run(f'FMOV.d #1.0, F0\nMTFS #{rounding << 16}\nFADD.d #1.1102230246251565e-16, F0')
            self.assertEqual(cpu.f[0], bits)
            self.assertTrue(cpu.fpsr & (1 << 0))
        cpu = run('ITOF.q.d #9007199254740993, F0')
        self.assertEqual(cpu.f[0], 0x4340000000000000)
        self.assertTrue(cpu.fpsr & (1 << 0))

    def test_fp_overflow_underflow_and_trap_rollback(self):
        cpu = run('FMOV.h #65504.0, F0\nMTFS #65536\nFMUL.h #2.0, F0')
        self.assertEqual(cpu.f[0], 0x7BFF)  # Toward zero gives the maximum finite value.
        self.assertTrue(cpu.fpsr & (1 << 2))
        cpu = run('FMOV.h #5.960464477539063e-8, F0\nFDIV.h #2.0, F0')
        self.assertEqual(cpu.f[0], 0)
        self.assertEqual(cpu.fpsr & ((1 << 1) | (1 << 0)), (1 << 1) | (1 << 0))
        cpu, _ = machine('FDIV.d #0.0, F0')
        cpu.f[0], cpu.fpsr = 0x3FF0000000000000, 1 << 11
        before = cpu.snapshot()
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 10)
        self.assertEqual(cpu.snapshot(), before)

class SystemTests(unittest.TestCase):
    def test_system_register_access(self):
        cpu = run('MTSR #0x123456, ASID\nMFSR ASID, R0\nMFSR ID, R1')
        self.assertEqual(cpu.r[:2], [0x3456, (48 << 46) | (52 << 40) | (6 << 32) | 0x3b01])
        cpu, _ = machine('MFSR VBR, R0')
        cpu.change_mode(1 << 12)
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 4)

    def test_user_syscall_banked_stacks_and_rte(self):
        cpu, p = machine('SYSCALL #42\nNOP\nhandler: MFSR FAULTINFO, R0\nRTE', handle_traps=True)
        cpu.memory.load(12 * 8, p.symbols['handler'].to_bytes(8, 'little'))
        cpu.sys['SPU'] = 0x8000
        cpu.change_mode(2 << 12)
        cpu.step()
        self.assertEqual(cpu.mode, 0)
        self.assertEqual(cpu.sys['SPU'], 0x8000)
        self.assertEqual(cpu.memory.read(cpu.r[31]+8, 8), 0x1003)
        cpu.step()
        cpu.step()
        self.assertEqual(cpu.r[0], 42)
        self.assertEqual(cpu.mode, 2)
        self.assertEqual(cpu.pc, 0x1003)
        self.assertEqual(cpu.r[31], 0x8000)

    def test_double_fault_cannot_commit_a_partial_frame(self):
        cpu, _ = machine('SYSCALL (R0)+', handle_traps=True)
        cpu.r[0], cpu.r[31] = 200, 12  # First push fits; second cannot.
        before, memory = cpu.snapshot(), bytes(cpu.memory.data)
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 14)
        self.assertEqual(cpu.snapshot(), before)
        self.assertEqual(bytes(cpu.memory.data), memory)

    def test_fault_handler_retries_original_instruction(self):
        cpu, p = machine('DIV.q R0, R1\nHALT\nhandler: MOV.q #2, R0\nRTE', handle_traps=True)
        cpu.r[1] = 20
        cpu.memory.load(9 * 8, p.symbols['handler'].to_bytes(8, 'little'))
        cpu.run(20)
        self.assertEqual(cpu.r[1], 10)
        self.assertEqual(cpu.sys['CAUSE'], 9)

    def test_interrupt_priority_and_wait(self):
        cpu, p = machine('WAIT\nHALT\nhandler: INC.q R0\nRTE')
        cpu.memory.load(64 * 8, p.symbols['handler'].to_bytes(8, 'little'))
        cpu.psr = 0
        cpu.step()
        self.assertTrue(cpu.waiting)
        cpu.interrupt(64, 3)
        cpu.run(10)
        self.assertEqual(cpu.r[0], 1)
        self.assertTrue(cpu.halted)
        cpu, _ = machine('WAIT')
        self.assertEqual(cpu.run(max_steps=1), 1)

    def test_interrupt_between_block_elements(self):
        cpu, p = machine('MOVS.b\nHALT\nhandler: INC.q R5\nRTE')
        cpu.memory.load(300, b'abc')
        cpu.memory.load(64 * 8, p.symbols['handler'].to_bytes(8, 'little'))
        cpu.r[:3], cpu.psr = [3, 300, 400], 0
        cpu.step()
        self.assertEqual(cpu.r[:3], [2, 301, 401])
        cpu.interrupt(64, 1)
        cpu.run(20)
        self.assertEqual(cpu.r[5], 1)
        self.assertEqual(cpu.memory.data[400:403], b'abc')

    def test_reset_rom(self):
        cpu = CPU(Memory(8192))
        cpu.memory.load(0, (8000).to_bytes(8, 'little') + (4096).to_bytes(8, 'little'))
        cpu.reset()
        self.assertEqual((cpu.pc, cpu.r[31], cpu.psr), (4096, 8000, 0x700))

    def setup_mmu(self, source='MOV.q @0x4000, R0\nHALT'):
        cpu = CPU(Memory(131072))
        p = assemble(source)
        cpu.memory.load(0x9000, p.code)
        # Four-level page tables rooted at 0x1000; map VA 0x1000 -> PA 0x9000.
        for address, value in [(0x1000, 0x2001), (0x2000, 0x3001), (0x3000, 0x4001),
                               (0x4008, 0x900F), (0x4020, 0xA007)]:
            cpu.memory.load(address, value.to_bytes(8, 'little'))
        cpu.sys['PTBR0'], cpu.sys['MMUCTL'], cpu.pc = 0x1000, 1, 0x1000
        return cpu

    def test_mmu_page_walk_and_ad_bits(self):
        cpu = self.setup_mmu('MOV.q #123, @0x4000\nMOV.q @0x4000, R0\nHALT')
        cpu.run()
        self.assertEqual(cpu.r[0], 123)
        self.assertEqual(cpu.memory.read(0xA000, 8), 123)
        self.assertTrue(cpu.memory.read(0x4008, 8) & 64)
        self.assertEqual(cpu.memory.read(0x4020, 8) & 192, 192)

    def test_mmu_fault_and_page_flag_rollback(self):
        cpu = self.setup_mmu('MOV.q #123, @0x4ffc')
        before = bytes(cpu.memory.data)
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 7)
        self.assertEqual(caught.exception.address, 0x5000)
        self.assertEqual(bytes(cpu.memory.data), before)
        self.assertEqual(cpu.pc, 0x1000)

    def test_mmu_permission_and_noncanonical_address(self):
        cpu = self.setup_mmu('MOV.q #123, @0x4000')
        cpu.memory.load(0x4020, (0xA003).to_bytes(8, 'little'))
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 8)
        cpu = self.setup_mmu('MOV.q @0x1000000000000, R0')
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 8)

    def test_large_pages_and_kernel_half(self):
        for shift, virtual in [(30, 0x5000), (21, 0x5000), (21, 0xFFFF800000005000)]:
            cpu = CPU(Memory(65536))
            root_index = (virtual >> 39) & 511
            cpu.memory.load(0x1000 + root_index * 8, (0x2001).to_bytes(8, 'little'))
            if shift == 21:
                cpu.memory.load(0x2000, (0x3001).to_bytes(8, 'little'))
            leaf_address = 0x2000 if shift == 30 else 0x3000
            cpu.memory.load(leaf_address, (0x22F).to_bytes(8, 'little'))
            cpu.memory.load(0x5000, b'large')
            cpu.sys['PTBR0'] = cpu.sys['PTBR1'] = 0x1000
            cpu.sys['MMUCTL'] = 1
            self.assertEqual(cpu.read(virtual, 5), int.from_bytes(b'large', 'little'))
            self.assertTrue(cpu.memory.read(leaf_address, 8) & 64)
class CLITests(unittest.TestCase):
    def test_binary_assembly_disassembly_execution(self):
        with tempfile.TemporaryDirectory() as temp:
            binary, symbol_map = Path(temp) / 'fib.bin', Path(temp) / 'fib.json'
            def command(*args):
                return subprocess.run([sys.executable, '-m', 'upsilon', *map(str, args)], cwd=ROOT,
                                      capture_output=True, text=True)
            result = command('asm', ROOT / 'examples/fibonacci.s', '-o', binary, '--map', symbol_map)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(symbol_map.read_text())['entry'], 0x1000)
            result = command('disasm', binary)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('ADD3.q R0, R1, R3', result.stdout)
            result = command('run', binary, '--binary', '--json')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['registers']['R0'], 6765)

    def test_limit_and_bad_source_fail_cleanly(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'loop.s'
            source.write_text('BRA .')
            result = subprocess.run([sys.executable, '-m', 'upsilon', 'run', str(source), '--max-steps', '10'],
                                    cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn('execution limit', result.stderr)
            self.assertNotIn('Traceback', result.stderr)

if __name__ == '__main__':
    unittest.main()
