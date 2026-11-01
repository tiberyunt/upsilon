"""Vectors and behavior derived from v0.2 Appendices A/B."""
import math
import unittest
from upsilon import assemble, CPU, Memory, Fault, AssemblyError
from upsilon.isa import *
from upsilon.decoder import decode


def run(source):
    cpu = CPU(Memory(65536))
    cpu.load(assemble(source+'\nHALT'))
    cpu.run(20000)
    return cpu

class V02Tests(unittest.TestCase):
    def test_appendix_opcode_vectors(self):
        vectors = {
            'HALT':'f606', 'DFLUSH (R0), #8':'f60520e8',
            'TLBI #0, #0':'f602e0e0', 'MFSR VBR, R0':'f600f000',
            'MTSR R0, PSR':'f60100f81b', 'MFFS R0':'f07000', 'MTFS #0':'f074e0',
            'FMOV.h F0, F1':'f0000001', 'FMOV.s F0, F1':'f0010001',
            'FMOV.d F0, F1':'f0020001', 'FNEG.d F0, F1':'f0220001',
            'FMA.d F0, F1, F2, F3':'f06200010203',
            'EXTZV #1, #2, R0, R1':'f201e1e20001',
            'BBS #1, R0, .':'f204e100fb', 'BBCCI #1, (R0), .':'f20ee120fb',
            'SCANN.b #0':'f110e0', 'ATXOR.q R0, (R1)':'f3170021',
            'BREV.q R0, R1':'f5130001', 'MULW.q R0, R1, R2, R3':'f52300010203',
            'UMULW.q R0, R1, R2, R3':'f52700010203',
            'DIVW.q #3, R4':'f52be304', 'UDIVW.q #3, R4':'f52fe304',
            'SOB R0, .':'f70000fc', 'SOBGE R0, .':'f70200fc',
            'AOBLE #4, R0, .':'f708e400fb', 'CASE R0, #0, #1':'f70c00e0e1',
            'CVTIF.q.d R0, F1':'f42e0001', 'CVTFI.d.q F0, R1':'f44b0001',
            'CVTFF.s.d F0, F1':'f4760001', 'MOVIF.q.d R0, F1':'f48e0001',
        }
        for source,hexbytes in vectors.items():
            with self.subTest(source=source):
                self.assertEqual(assemble(source).code.hex(),hexbytes)

    def test_reserved_optional_fp_and_invalid_fixed_types(self):
        for raw in (b'\xf0\x03',b'\xf0\x71',b'\xf4\x2f',b'\xf5\x0c',b'\xf7\x0d'):
            with self.subTest(raw=raw), self.assertRaises(Fault):
                decode(lambda address:raw[address],0)
        for source in ('FMOV.x F0,F1','CVTIF.q.x R0,F0','MOVIF.l.d R0,F0','BSWAP.b R0,R1',
                       'MFSR (R0),R1','CASE R0,#0,table\ntable: NOP'):
            with self.subTest(source=source), self.assertRaises(AssemblyError):
                assemble(source)

    def test_all_short_fp_literals_and_signed_zero(self):
        for i,value in enumerate(FP_CONSTANTS):
            cpu = CPU(Memory(65536))
            cpu.memory.load(4096,bytes([0xf0,2,0xe0+i,0]))
            cpu.pc = 4096
            cpu.step()
            actual = cpu.bits_float(cpu.f[0],8)
            self.assertTrue(math.isnan(actual) if math.isnan(value) else actual==value)
            if value == 0:
                self.assertEqual(math.copysign(1.,actual),math.copysign(1.,value))

    def test_scann_and_movs_final_pointers(self):
        cpu = CPU(Memory(65536))
        p=assemble('MOVS.b\nHALT')
        cpu.load(p)
        cpu.memory.load(300,b'abcdefghij')
        cpu.r[:3]=[8,300,302]
        cpu.run()
        self.assertEqual(cpu.r[:3],[0,308,310])
        self.assertEqual(cpu.memory.data[300:310],b'ababcdefgh')
        cpu.load(assemble('SCANN.b #65\nHALT'))
        cpu.memory.load(300,b'AAAB')
        cpu.r[:2]=[4,300]
        cpu.run()
        self.assertEqual(cpu.r[:2],[1,303])
        self.assertTrue(cpu.psr&Z)

    def test_atomic_logic_and_bit_clear(self):
        cpu=run('MOV.q #15,@256\nMOV.q #3,R0\nATAND.q R0,@256\nMOV.q #8,R1\nATOR.q R1,@256\nMOV.q #1,R2\nATXOR.q R2,@256\nBBCCI #64,@256,next\nMOV.q #99,R3\nnext: BBSSI #1,@256,done\nMOV.q #99,R4\ndone: NOP')
        self.assertEqual(cpu.r[:5],[15,3,11,0,0])
        self.assertEqual(cpu.memory.read(256,8),10)
        cpu=run('MOV.q #1,R0\nEXTZV #64,#1,R0,R1\nBBCC #64,R0,next\nNOP\nnext: NOP')
        self.assertEqual(cpu.r[:2],[0,1])

    def test_signed_unsigned_wide_operations_and_faults(self):
        cpu=run('MULW.b #-2,#3,R0,R1\nUMULW.b #254,#3,R2,R3\nMOV.b #246,R4\nMOV.b #255,R5\nDIVW.b #3,R4')
        self.assertEqual(cpu.r[:6],[250,255,250,2,253,255])
        for instruction in ('DIVW.q #0,R0','UDIVW.b #1,R0','DIVW.q #1,R31'):
            cpu=CPU(Memory(65536));cpu.load(assemble(instruction));cpu.r[1]=1
            before=cpu.snapshot()
            with self.assertRaises(Fault):cpu.step()
            self.assertEqual(cpu.snapshot(),before)

    def test_loop_variants(self):
        cpu=run('MOV.q #3,R0\na: INC.q R1\nSOBGE R0,a\nMOV.q #3,R2\nb: INC.q R3\nSOBGT R2,b\nc: AOBLT #3,R4,c\nd: AOBLE #3,R5,d')
        self.assertEqual(cpu.r[:6],[MASK64,4,0,3,3,4])

    def test_psr_bank_switch_and_system_numbers(self):
        cpu=run('MTSR #8192,SPU\nMFSR SPU,R0\nMFSR ID,R1\nTLBI #0,#0')
        self.assertEqual(cpu.r[:2],[8192,(48 << 46) | (52 << 40) | (6 << 32) | 0x3b01])
        cpu.load(assemble('MTSR #8192,PSR'))
        cpu.step()
        self.assertEqual(cpu.mode,2)
        self.assertEqual(cpu.r[31],8192)
        cpu=CPU(Memory(65536));cpu.load(assemble('TLBI #4,#0'))
        with self.assertRaises(Fault):cpu.step()

    def test_fp_unary_ternary_and_rounding(self):
        cpu=run('FSQRT.d #2.0,F0\nFADD3.d #2.0,#3.0,F1\nFSUB3.d #2.0,#3.0,F2\nFMUL3.d #2.0,#3.0,F3\nFDIV3.d #2.0,#3.0,F4\nFFLOOR.d #-2.75,F5\nFCEIL.d #-2.75,F6\nFTRUNC.d #-2.75,F7\nFREM.d #2.0,F4')
        floats=[cpu.bits_float(v,8) for v in cpu.f[:8]]
        self.assertEqual(floats,[math.sqrt(2),5.,1.,6.,-.5,-3.,-2.,-2.])

    def test_fused_single_rounding(self):
        # (1+2^-27)*(1-2^-27)-1 = -2^-54, lost by separate binary64 multiply/add.
        cpu=run('FMA.d #1.0000000074505806,#0.9999999925494194,#-1.0,F0\nFMS.d #2.,#3.,#4.,F1\nFNMA.d #2.,#3.,#4.,F2\nFNMS.d #2.,#3.,#4.,F3')
        self.assertEqual([cpu.bits_float(v,8) for v in cpu.f[:4]],[-2**-54,2.,-2.,-10.])

    def test_float_conversion_saturates_and_sets_flags(self):
        cpu=run('CVTFI.d.b #999.,R0\nCVTFI.d.b #-999.,R1\nCVTFU.d.b #-1.,R2\nCVTFI.d.q #nan,R3\nMTFS #131072\nCVTFIR.d.q #2.25,R4\nCVTFI.d.q #2.75,R5\nCVTUF.q.d #-1,F0')
        self.assertEqual(cpu.r[:6],[127,128,0,0,3,2])
        self.assertTrue(cpu.fpsr&1)
        self.assertEqual(cpu.bits_float(cpu.f[0],8),float(2**64))

    def test_signaling_nan_class_compare_and_raw_moves(self):
        cpu=run('MOV.q #0x7ff0000000000001,R0\nMOVIF.q.d R0,F0\nFCLASS.d F0,R1\nMOVFI.d.q F0,R2\nFCMP.d F0,F0\nMFFS R3')
        self.assertEqual(cpu.r[1],1<<8)
        self.assertEqual(cpu.r[2],0x7ff0000000000001)
        self.assertTrue(cpu.r[3]&16)
        self.assertEqual(cpu.psr&15,V)
        cpu=run('FCMPE.d #nan,F0')
        self.assertTrue(cpu.fpsr&16)

    def test_minmax_signed_zeros_class_and_copy_sign(self):
        cpu=run('FMOV.d #-0.0,F0\nFMAX.d #0.0,F0\nFMOV.d #0.0,F1\nFMIN.d #-0.0,F1\nFCLASS.d F0,R0\nFCLASS.d F1,R1\nFCOPYSIGN.d #-1.0,F0\nFTST.d F0')
        self.assertEqual(cpu.r[:2],[1<<4,1<<3])
        self.assertEqual(cpu.f[0],1<<63)
        self.assertEqual(cpu.psr&15,Z)
