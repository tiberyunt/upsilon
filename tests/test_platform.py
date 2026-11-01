"""Workstation ELF boot and NS16550 platform regression checks."""
from pathlib import Path
import json, os, struct, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
TOOL=Path(os.environ.get('UPSILON_C',ROOT/'build/upsilon'))
def image(segments=None,entry=0x200000):
    if segments is None:segments=[(0x200000,b'\xf6\x06',32,5)]
    h=bytearray(64+56*len(segments));h[:7]=b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQQIHHHHHH',h,16,2,0xf5a0,1,entry,64,0,0,64,56,len(segments),0,0,0)
    for i,(pa,data,memsz,flags) in enumerate(segments):
        off=len(h);h.extend(data)
        struct.pack_into('<IIQQQQQQ',h,64+56*i,1,flags,off,pa,pa,len(data),memsz,1)
    return h
class PlatformTests(unittest.TestCase):
    def execute(self,data,*args,success=True):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'kernel.elf').write_bytes(data)
            command=[str(TOOL),'-m','16','--kernel',str(p/'kernel.elf'),'--no-keyboard','--max-steps','10000','--state',str(p/'state.json'),*args]
            r=subprocess.run(command,text=True,capture_output=True)
            if success:
                self.assertEqual(r.returncode,0,r.stderr+r.stdout)
                return json.loads((p/'state.json').read_text()),r
            self.assertNotEqual(r.returncode,0);return r
    def test_kernel_elf_handoff(self):
        s,r=self.execute(image(),'--bootargs','-s')
        self.assertTrue(s['halted']);self.assertEqual(s['registers']['R0'],16*1024*1024-65536)
        self.assertEqual(s['registers']['R1'],0);self.assertEqual(s['system_registers']['MMUCTL'],0)
        self.assertNotIn('Booting',r.stdout)
    def test_kernel_zero_fills_bss(self):
        # Read the zero-filled portion into R0, then halt.
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'a.s').write_text('MOV.q @0x300000,R0\nHALT\n')
            r=subprocess.run([str(TOOL),'asm',str(p/'a.s'),'-o',str(p/'a.bin')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            s,_=self.execute(image([(0x200000,(p/'a.bin').read_bytes(),64,5),(0x300000,b'',64,6)]))
            self.assertEqual(s['registers']['R0'],0)
    def test_rejects_bad_elf_headers(self):
        for offset,value in [(0,0),(4,1),(5,2),(16,3),(18,0),(20,2),(52,0),(54,0),(56,0)]:
            with self.subTest(offset=offset):
                b=image();b[offset]=value;self.execute(b,success=False)
    def test_rejects_truncated_program_headers(self):
        self.execute(image()[:70],success=False)
        b=image();struct.pack_into('<Q',b,32,2**64-1);self.execute(b,success=False)
    def test_rejects_invalid_segment_bounds(self):
        cases=[(0x200000,b'abc',2,5),(16*1024*1024,b'abc',64,5),(2**64-1,b'',2,5)]
        for case in cases:
            with self.subTest(case=case):self.execute(image([case]),success=False)
    def test_rejects_overlap_and_fdt_collision(self):
        self.execute(image([(0x200000,b'\xf6\x06',64,5),(0x200020,b'',64,6)]),success=False)
        self.execute(image([(0x200000,b'\xf6\x06',64,5),(16*1024*1024-65536,b'',64,6)]),success=False)
    def test_rejects_nonexecutable_and_bss_entries(self):
        self.execute(image([(0x200000,b'\xf6\x06',32,6)]),success=False)
        self.execute(image(entry=0x200008),success=False)
    def test_rejects_dynamic_images_and_bad_alignment(self):
        b=image();struct.pack_into('<I',b,64,3);self.execute(b,success=False)
        b=image();struct.pack_into('<Q',b,112,3);self.execute(b,success=False)
    def test_oversized_bootargs(self):
        self.execute(image(),'--bootargs','x'*1024,success=False)
    def test_elf_command_requires_path(self):
        p=subprocess.run([str(TOOL),'--elf'],capture_output=True,text=True)
        self.assertEqual(p.returncode,2);self.assertIn('image path',p.stderr)
    def test_ns16550_serial_and_divisor(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);source='''MOV.b #128,@0xffff0403
MOV.b #52,@0xffff0400
MOV.b #18,@0xffff0401
MOV.b @0xffff0400,R0
MOV.b @0xffff0401,R1
MOV.b #3,@0xffff0403
MOV.b #72,@0xffff0400
MOV.b #105,@0xffff0400
MOV.b @0xffff0405,R2
HALT
'''
            (p/'uart.s').write_text(source)
            r=subprocess.run([str(TOOL),'run',str(p/'uart.s'),'--serial','--state',str(p/'state.json')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            s=json.loads((p/'state.json').read_text());self.assertEqual(s['registers']['R0'],52);self.assertEqual(s['registers']['R1'],18)
            self.assertEqual(s['registers']['R2'],0x60);self.assertTrue(r.stdout.startswith('Hi'))
if __name__=='__main__':unittest.main(verbosity=2)
