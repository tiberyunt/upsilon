"""v0.4 golden encodings, shared by the C host and the native ROM assembler."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('UPSILON_C', ROOT / 'build/upsilon')).resolve()


def encodings():
    cases = [
        ('MFTP R0', 'ee00'), ('PAUSE', 'f880'), ('PREFETCH (R1)', 'f88121'),
        ('PREFETCHW 8(TP)', 'f882ff6008000000'),
        ('MOV.q -4(TP),R0', '03ff60fcffffff00'),
        ('MOV.q (TP)[R1],R2', '03c1ff600000000002'),
        ('CAS2 R0,R2,(R4)', 'f31b000224'),
        ('IOFENCE', 'f607'), ('UAON', 'f608'), ('UAOFF', 'f609'),
        ('MOVZ.bq R1,R2', 'f4030102'),
        ('MOV.q *8(R1),R2', '03ff410800000002'),
        ('.bss\n.globl value\nvalue: .quad 42', '2a00000000000000'),
    ]
    for cond, name in enumerate('EQ NE LT LE GT GE LO LS HI HS MI PL VS VC AL'.split()):
        for code, size in enumerate('bwlq'):
            cases.append((f'SET{name}.{size} R0', bytes([0xf8, cond*4+code, 0]).hex()))
            cases.append((f'CMOV{name}.{size} R1,R2', bytes([0xf8, 0x40+cond*4+code, 1, 2]).hex()))
    for name, number in [('TP',7), ('ESP',8), ('TIMECMP',28), ('MCSTATUS',29),
                         ('MPUCTL',40), ('PRBASE15',79), ('PRLIM15',95),
                         ('PRATTR15',111), ('WPADDR3',115), ('WPCTL3',119)]:
        immediate = bytes([0xe0+number]) if number<24 else bytes([0xf8,number])
        cases.append((f'MFSR {name},R0', (b'\xf6\0'+immediate+b'\0').hex()))
    return cases


class V04CTests(unittest.TestCase):
    def command(self, *args):
        result = subprocess.run([str(EXE), *map(str,args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def test_host_golden_encodings_and_disassembly(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'a.s';binary=Path(directory)/'a.bin'
            for text, expected in encodings():
                with self.subTest(source=text):
                    source.write_text(text)
                    self.command('asm',source,'-o',binary)
                    self.assertEqual(binary.read_bytes(),bytes.fromhex(expected))
            for text, _ in encodings():
                if text.startswith('.'):continue
                with self.subTest(disassembly=text):
                    source.write_text(text)
                    self.command('asm',source,'-o',binary)
                    # CLI listings are human-readable; exercise decode, including TP and F8.
                    listing=self.command('disasm',binary).stdout
                    self.assertNotIn('.byte',listing)

    def test_native_golden_encodings(self):
        cases=encodings()
        with tempfile.TemporaryDirectory() as directory:
            dump=Path(directory)/'ram'
            for at in range(0,len(cases),8):
                batch=cases[at:at+8]
                session=''.join(f'edit f{i}.s\n{text}\n.save\nasm f{i}.s\n' for i,(text,_) in enumerate(batch))+'shutdown\n'
                result=self.command('run',ROOT/'build/firmware_rom.bin','--binary','--boot','--serial',
                                    '--serial-input',session,'--max-steps','100000000','--dump-memory',dump)
                self.assertNotIn('?ASSEMBLY ERROR',result.stdout)
                ram=dump.read_bytes()
                for i,(text,expected) in enumerate(batch):
                    with self.subTest(source=text):
                        slot=0x50000+i*0x4000
                        length=struct.unpack_from('<Q',ram,slot+40)[0]
                        self.assertEqual(ram[slot+0x2040:slot+0x2040+length],bytes.fromhex(expected))

    def test_reserved_encodings_and_fpd(self):
        for text, vector in [('.byte 0xf8,0x3c,0',1),('.byte 0xf8,0x7f,0,1',1),
                             ('.byte 0xf8,0x83',1),('MFSR #9,R0',1),
                             ('MTSR #0,#120',1),('FMOV.s @0xffff0000,F0',15),
                             ('.byte 0xf3,0x18,0,2,0x24',1)]:
            with self.subTest(source=text),tempfile.TemporaryDirectory() as directory:
                source=Path(directory)/'a.s';state=Path(directory)/'state.json';source.write_text(text)
                result=subprocess.run([str(EXE),'run',str(source),'--set','PSR=65536','--state',str(state)],capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(f'vector {vector},',result.stderr)
                self.assertEqual(json.loads(state.read_text())['pc'],4096)
