"""Read-only SIMH reel framing, native tape boot, and emulator-style CLI."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from upsilon import assemble
ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('UPSILON_C',ROOT/'build/upsilon')).resolve()


def record(data,error=False):
    header=struct.pack('<I',len(data)|(0x80000000 if error else 0))
    return header+data+(b'\0' if len(data)&1 else b'')+header


def boot_header(length,load=0x80000,entry=None,stack=0,version=1):
    return b'UPS9BOOT'+struct.pack('<5Q',version,load,load if entry is None else entry,length,stack)

class TapeTests(unittest.TestCase):
    def run_tape(self,media,*args,expected=0,input='shutdown\n'):
        with tempfile.TemporaryDirectory() as directory:
            reel=Path(directory)/'reel.tap';reel.write_bytes(media)
            result=subprocess.run([str(EXE),'-m','2','-t',str(reel),*map(str,args)],
                                  input=input,capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,expected,result.stderr)
            self.assertEqual(reel.read_bytes(),media)
            return result

    def test_requested_cli_boots_multirecord_odd_length_reel(self):
        program=assemble((ROOT/'examples/tape_hello.s').read_text(),origin=0x100000)
        with tempfile.TemporaryDirectory() as directory:
            raw=Path(directory)/'payload.bin';reel=Path(directory)/'exampletape'
            raw.write_bytes(program.code)
            made=subprocess.run([str(EXE),'mktape',str(raw),'-o',str(reel),'--load','0x100000',
                                 '--record-size','17'],capture_output=True,text=True)
            self.assertEqual(made.returncode,0,made.stderr)
            image=reel.read_bytes()
            result=subprocess.run([str(EXE),'-m','256','-t',str(reel),'--json'],
                                  input='',capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stderr)
            state=json.loads(result.stdout)
            self.assertEqual(state['memory_bytes'],256*1024*1024)
            self.assertEqual(state['registers']['R31'],256*1024*1024)
            self.assertTrue(state['tape']['mounted'])
            self.assertEqual(state['tape']['status'],5) # online and tape mark
            self.assertIn('Booting tape0',result.stderr)
            self.assertIn('Hello from 9-track tape\n',result.stderr)
            self.assertEqual(reel.read_bytes(),image)

    def test_emulator_binary_contains_rom_and_is_relocatable(self):
        program=assemble('MOV.q #123,R0\nHALT',origin=0x80000)
        media=record(boot_header(len(program.code)))+record(program.code)+struct.pack('<II',0,0xffffffff)
        with tempfile.TemporaryDirectory() as directory:
            executable=Path(directory)/'upsilon';reel=Path(directory)/'exampletape'
            shutil.copy2(EXE,executable);reel.write_bytes(media)
            result=subprocess.run([str(executable),'-m','4','-t','exampletape','--json'],cwd=directory,
                                  input='',capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(json.loads(result.stdout)['registers']['R0'],123)

    def test_manual_monitor_boot_and_case_insensitive_alias(self):
        code=assemble('MOV.q #42,R0\nHALT',origin=0x80000).code
        media=record(boot_header(len(code)))+record(code)+struct.pack('<I',0)
        result=self.run_tape(media,'--no-autoboot','--json',input='boot MT0\n')
        self.assertIn('OK> boot MT0',result.stderr)
        self.assertEqual(json.loads(result.stdout)['registers']['R0'],42)

    def test_no_tape_and_unknown_device_recover_to_prompt(self):
        result=subprocess.run([str(EXE),'-m','2','--json'],input='boot tape0\nboot disk0\nshutdown\n',
                              capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('?TAPE0 OFFLINE',result.stderr)
        self.assertIn('DISK0 NOT BOOTABLE',result.stderr)
        self.assertFalse(json.loads(result.stdout)['tape']['mounted'])

    def test_empty_tapemark_and_unknown_boot_headers(self):
        for media in (b'',struct.pack('<I',0),record(b'not a boot header'),
                      record(boot_header(2,version=2))+record(b'\xf6\x06')):
            with self.subTest(media=media):
                result=self.run_tape(media)
                self.assertIn('?TAPE NOT BOOTABLE',result.stdout)
                self.assertIn('OK> shutdown',result.stdout)

    def test_corrupt_framing_is_rejected_before_execution(self):
        for media in (b'\x01',struct.pack('<I',3)+b'abc',struct.pack('<I',2)+b'ab'+struct.pack('<I',3),
                      struct.pack('<I',0xffffffff)+b'extra',struct.pack('<I',0x01000001)):
            with self.subTest(media=media):
                result=self.run_tape(media,expected=1,input='')
                self.assertIn('tape',result.stderr)
                self.assertNotIn('Kotl Upsilon firmware',result.stdout)

    def test_erase_gaps_and_physical_eof(self):
        code=assemble('MOV.q #7,R0\nHALT',origin=0x80000).code
        media=struct.pack('<I',0xfffffffe)+record(boot_header(len(code)))+struct.pack('<I',0xfffffffe)+record(code)
        state=json.loads(self.run_tape(media,'--json',input='').stdout)
        self.assertEqual(state['registers']['R0'],7)
        self.assertEqual(state['tape']['status'],9) # online and EOM

    def test_record_error_and_length_validation_do_not_load_partial_image(self):
        code=assemble('MOV.q #77,R0\nHALT',origin=0x80000).code
        images=[record(boot_header(len(code)))+record(code,error=True),
                record(boot_header(len(code)+1))+record(code)+struct.pack('<I',0),
                record(boot_header(len(code)-1))+record(code)+struct.pack('<I',0),
                record(boot_header(len(code)))+record(code)+record(b'extra')+struct.pack('<I',0)]
        with tempfile.TemporaryDirectory() as directory:
            dump=Path(directory)/'ram'
            for media in images:
                with self.subTest(media=media):
                    result=self.run_tape(media,'--dump-memory',dump)
                    self.assertTrue('?TAPE RECORD ERROR' in result.stdout or '?TAPE BOOT LENGTH MISMATCH' in result.stdout)
                    self.assertEqual(dump.read_bytes()[0x80000:0x80000+len(code)],b'\0'*len(code))

    def test_boot_address_stack_and_entry_validation(self):
        code=b'\xf6\x06'
        headers=[boot_header(2,load=0x1000),boot_header(2,load=0x200000),
                 boot_header(2,entry=0x80002),boot_header(2,stack=0x90001),
                 boot_header(2,stack=0x300000),boot_header(2,stack=0x80000),boot_header(0)]
        for header in headers:
            with self.subTest(header=header):
                result=self.run_tape(record(header)+record(code)+struct.pack('<I',0))
                self.assertIn('?TAPE BOOT ADDRESS OR STACK',result.stdout)

    def test_custom_stack_and_tape_attachment_in_run_mode(self):
        code=assemble('MOV.q #9,R0\nHALT',origin=0x80000).code
        with tempfile.TemporaryDirectory() as directory:
            reel=Path(directory)/'tape';reel.write_bytes(record(boot_header(len(code),stack=0x180000))+record(code)+struct.pack('<I',0))
            result=subprocess.run([str(EXE),'run',str(ROOT/'build/firmware_rom.bin'),'--binary','--boot',
                                   '-m','2','-t',str(reel),'--keyboard','--json'],input='boot tape\n',
                                  capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stderr)
            state=json.loads(result.stdout)
            self.assertEqual(state['registers']['R31'],0x180000)
            self.assertEqual(state['registers']['R0'],9)

    def test_bad_memory_option_and_mktape_arguments(self):
        for value in ('0','4096','18446744073709551615'):
            result=subprocess.run([str(EXE),'-m',value],input='',capture_output=True,text=True)
            self.assertEqual(result.returncode,2,result.stderr)
        with tempfile.TemporaryDirectory() as directory:
            raw=Path(directory)/'a.bin';raw.write_bytes(b'\xf6\x06')
            for flag,value in (('--record-size','0'),('--record-size','65537'),('--load','4096'),('--entry','0')):
                result=subprocess.run([str(EXE),'mktape',str(raw),'-o',str(Path(directory)/'a.tap'),flag,value],
                                      capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)
