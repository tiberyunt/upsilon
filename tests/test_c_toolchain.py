"""C host and guest assembly differential checks against the Python oracle."""
import ast
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest

from upsilon import assemble, CPU, Memory
from upsilon.isa import *
ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('UPSILON_C',ROOT/'build/upsilon')).resolve()
ROM=ROOT/'build/firmware_rom.bin'


def cases():
    sources=[]
    for sz in SIZES:
        for name in TWO:
            if name=='LEA' and sz!='q':continue
            args='(R0), R1' if name=='LEA' else 'R0' if name in ONE else '#42, R1'
            sources.append(f'{name}.{sz} {args}')
        for name in THREE:sources.append(f'{name}3.{sz} #3,R1,R2')
    sources.extend(n+' .' for n in BRANCHES)
    for name in FP_OPS:
        for sz in FLOAT_SIZES:
            args=', '.join(('F' if is_float(name,i) else 'R')+str(i) for i in range(operand_count(name)))
            sources.append(f'{name}.{sz} {args}')
    for name,(page,op,count) in EXT.items():
        if page==0xf0:continue
        for sz in SIZES:
            if name=='BSWAP' and sz=='b':continue
            if page==0xf1:args='' if count==0 else '#1'
            elif page==0xf3:args='R0,R1,(R2)' if count==3 else 'R0,(R1)'
            elif name in ('DIVW','UDIVW'):args='#3,R0'
            else:args=','.join(f'R{i}' for i in range(count))
            sources.append(f'{name}.{sz} {args}')
    for name in CONVERSIONS:
        for src,ss in (FLOAT_SIZES if is_float(name,0) else SIZES).items():
            for dst,ds in (FLOAT_SIZES if is_float(name,1) else SIZES).items():
                if name in ('MOVIF','MOVFI') and ss!=ds:continue
                args=('F0' if is_float(name,0) else 'R0')+','+('F1' if is_float(name,1) else 'R1')
                sources.append(f'{name}.{src}.{dst} {args}')
    for name in BIT_BRANCHES:sources.append(f'{name} #1,R0,.+200')
    for name in LOOPS:
        args='R0' if name.startswith('SOB') else '#3,#1,R0' if name=='ACB' else '#3,R0'
        sources.append(f'{name} {args},.+200')
    sources+=['MFFS R0','MTFS #0','DFLUSH (R0),#8','MOV.q (PC),R0','.space 3,-1']
    sources+=['MOV.q (R0)[R1],R2','MOV.q @0x123456789abcdef0[R2],R3',
        'MOV.q [8(R2)][R3],R4','MOV.q 65536(R1),R0','MOV.q -0x1234567890(R1),R0',
        'MOV.b #255,R0',".byte '.', ';'",'start: MOV.q #end,R0\nMOV.q start,R1\nBRA end\n.space 130\nend: HALT',
        '.equ a,42\nMOV.q #a*2-1,R0','.ascii "a;b", "c"\n.asciz "!"',
        '.align 8\n.quad 0x123456789abcdef0','MOVSX.b.q #-1,R0','CVTIF.q.d R0,F1',
        'CVTFI.d.q F0,R1','MOVIF.q.d R0,F1','MFSR PSR,R0','MTSR R0,SCRATCH3',
        'TLBI #0,#0','SOB R0,.','ACB #4,#2,R0,.','EXTZV #1,#2,R0,R1',
        'BBS #1,R0,.','BBCCI #64,@256,.','CAS.q R0,R1,@256','UDIVW.q #3,R0',
        'MULW.q R0,R1,R2,R3']
    for x in ('1.5','0.1','-0.0','nan','inf','-inf','3.141592653589793',
              '1.0000000074505806','0.9999999925494194','1e100','1e-300',
              '5e-324','2.2250738585072014e-308','1.7976931348623157e308','.5'):
        sources.append(f'FMOV.d #{x},F0')
    return sources

class CToolchainTests(unittest.TestCase):
    def command(self,*args,**kwargs):
        result=subprocess.run([str(EXE),*map(str,args)],capture_output=True,text=True,**kwargs)
        self.assertEqual(result.returncode,0,result.stderr)
        return result

    def test_host_assembler_encoding_matrix(self):
        with tempfile.TemporaryDirectory() as directory:
            src=Path(directory)/'a.s';out=Path(directory)/'a.bin'
            for source in cases():
                with self.subTest(source=source):
                    src.write_text(source)
                    self.command('asm',src,'-o',out,'--origin','0x80000')
                    self.assertEqual(out.read_bytes(),assemble(source,origin=0x80000).code)

    def test_native_assembler_encoding_matrix_without_host_calls(self):
        with tempfile.TemporaryDirectory() as directory:
            dump=Path(directory)/'ram'
            sources=cases()
            for at in range(0,len(sources),8):
                batch=sources[at:at+8]
                session=''.join(f'edit f{i}.s\n{s}\n.save\nasm f{i}.s\n' for i,s in enumerate(batch))+'shutdown\n'
                self.command('run',ROM,'--binary','--boot','--serial','--serial-input',session,
                             '--max-steps','100000000','--dump-memory',dump,timeout=30)
                ram=dump.read_bytes()
                for i,source in enumerate(batch):
                    slot=0x50000+i*0x4000
                    length=struct.unpack_from('<Q',ram,slot+40)[0]
                    with self.subTest(source=source):
                        self.assertEqual(ram[slot+0x2040:slot+0x2040+length],assemble(source,origin=0x80000).code)

    def test_c_emulator_matches_existing_program_fixtures(self):
        sources=[]
        for file in ('test_toolchain.py','test_v02.py'):
            for node in ast.walk(ast.parse((ROOT/'tests'/file).read_text())):
                if (isinstance(node,ast.Call) and isinstance(node.func,ast.Name) and node.func.id=='run'
                    and node.args and isinstance(node.args[0],ast.Constant) and isinstance(node.args[0].value,str)):
                    sources.append(node.args[0].value)
        # Width/flag boundaries, all FP formats and exact tie-breaking in each mode.
        for size in SIZES:
            bits=SIZES[size]*8
            sources+= [f'MOV.{size} #{(1<<bits)-1},R0\nADD.{size} #1,R0',
                       f'MOV.{size} #{(1<<(bits-1))-1},R0\nADD.{size} #1,R0']
        for size in FLOAT_SIZES:
            for mode in range(4):
                sources.append(f'FMOV.{size} #1.0,F0\nMTFS #{mode<<16}\nFADD.{size} #1.1102230246251565e-16,F0')
        sources += ['FSQRT.h #1e100,F0','FSQRT.s #1e100,F0','FSQRT.h #5e-324,F0']
        for mode in range(4):
            sources.append(f'MTFS #{mode<<16}\nCVTUF.q.s #9007199791611905,F0\nFSQRT.h #2.0,F1')
        with tempfile.TemporaryDirectory() as directory:
            src=Path(directory)/'program.s';dump=Path(directory)/'ram'
            for source in sources:
                source+='\nHALT'
                reference=CPU(Memory(65536));reference.load(assemble(source))
                reference.run(20000)
                src.write_text(source)
                with self.subTest(source=source):
                    result=self.command('run',src,'--json','--memory','65536','--dump-memory',dump)
                    state=json.loads(result.stdout)
                    self.assertEqual([state['registers'][f'R{i}'] for i in range(32)],reference.r)
                    self.assertEqual([state['float_bits'][f'F{i}'] for i in range(32)],reference.f)
                    self.assertEqual(state['psr'],reference.psr)
                    self.assertEqual(state['fpsr'],reference.fpsr)
                    self.assertEqual(dump.read_bytes(),reference.memory.data)

    def test_native_function_handles_large_mantissa_without_editor(self):
        # Direct guest JSR exercises the documented 128-digit native ABI beyond
        # the monitor editor's smaller 120-character line length.
        from upsilon.firmware import read_native_source
        literal='3'+'0'*127+'e489'
        text=f'FMOV.d #{literal},F0\n'
        driver='.entry start\n.quad 0x100000,start\nstart: MOV.q #input_text,R0\n'
        driver+=f'MOV.q #{len(text)},R1\nMOV.q #0x30000,R2\nMOV.q #8128,R3\n'
        driver+='MOV.q #0x40000,R4\nMOV.q #256,R5\nMOV.q #0x80000,R6\nJSR na_assemble\nHALT\n'
        driver+=read_native_source(ROOT/'examples/native_assembler.s')
        driver+='\n.org 0x20000\ninput_text: .asciz '+json.dumps(text)+'\n'
        with tempfile.TemporaryDirectory() as directory:
            src=Path(directory)/'driver.s';dump=Path(directory)/'ram'
            src.write_text(driver)
            result=self.command('run',src,'--boot','--json','--max-steps','10000000','--dump-memory',dump,timeout=20)
            state=json.loads(result.stdout)
            self.assertEqual(state['registers']['R0'],0)
            length=state['registers']['R1']
            self.assertEqual(dump.read_bytes()[0x30000:0x30000+length],assemble(text,origin=0x80000).code)

    def test_native_diagnostics_preserve_old_binary_and_file(self):
        session='edit a.s\nNOP\n.save\nasm a.s\nedit a.s\nBOGUS\n.cancel\nasm a.s\nedit bad.s\nMOV.q #missing,R0\n.save\nasm bad.s\nshutdown\n'
        result=self.command('run',ROM,'--binary','--boot','--serial','--serial-input',session)
        self.assertIn('undefined symbol',result.stdout)
        self.assertEqual(result.stdout.count('Assembled 1 bytes'),2)

    def test_native_invalid_operands_and_source_diagnostics(self):
        invalid=['MFSR R0,R1','CASE R0,#0,R1','MOVIF.l.d R0,F1',
                 'MOV.q (R0)+[R1],R2','.align 0','BOGUS',
                 '.equ x,1\nx: NOP','MOV.b #256,R0']
        session=''.join(f'edit bad{i}.s\n{s}\n.save\nasm bad{i}.s\n' for i,s in enumerate(invalid))+'shutdown\n'
        result=self.command('run',ROM,'--binary','--boot','--serial','--serial-input',session)
        self.assertEqual(result.stdout.count('?ASSEMBLY ERROR:'),len(invalid))

    def test_native_source_and_host_code_have_no_assembly_service(self):
        monitor=(ROOT/'examples/monitor.s').read_text()
        self.assertIn('JSR na_assemble',monitor)
        self.assertNotIn('SYSCALL #',monitor)
        for name in ('native_assembler.s','native_float.inc'):
            self.assertNotIn('SYSCALL #',(ROOT/'examples'/name).read_text())
        self.assertNotIn('firmware_services',(ROOT/'simulator/cpu.c').read_text())
        self.assertNotIn('assemble(', (ROOT/'simulator/cpu.c').read_text())
