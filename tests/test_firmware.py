"""Guest OK monitor, RAM files and explicit assembler service integration."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from upsilon import assemble, CPU, Memory, SerialConsole, Fault
from upsilon.firmware import compose_firmware, read_native_source
import json

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = 0x50000
SLOT = 0x4000
SOURCE = 64
BINARY = 0x2040

class FirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = compose_firmware(read_native_source(ROOT/'examples/monitor.s'),
                                      (ROOT/'examples/basic_rom.s').read_text(),
                                      read_native_source(ROOT/'examples/native_assembler.s'))
        cls.program = assemble(cls.source)

    def firmware(self, session, max_steps=1000000):
        with tempfile.TemporaryDirectory() as temp:
            rom=Path(temp)/'rom.bin';dump=Path(temp)/'ram.bin'
            rom.write_bytes(self.program.code)
            result=subprocess.run([str(ROOT/'build/upsilon'),'run',str(rom),'--binary','--boot',
                                   '--serial','--serial-input',session,'--json','--max-steps',str(max_steps),
                                   '--dump-memory',str(dump)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr)
            state=json.loads(result.stdout)
            cpu=CPU(Memory(serial=SerialConsole()))
            cpu.memory.data[:]=dump.read_bytes()
            cpu.pc=state['pc'];cpu.psr=state['psr'];cpu.fpsr=state['fpsr']
            cpu.r=[state['registers'][f'R{i}'] for i in range(32)]
            cpu.f=[state['float_bits'][f'F{i}'] for i in range(32)]
            cpu.halted=state['halted']
            self.assertTrue(cpu.halted)
            return cpu,result.stderr

    def file(self, cpu, index=0):
        addr = DIRECTORY+index*SLOT
        source_len = cpu.memory.read(addr+32,8)
        binary_len = cpu.memory.read(addr+40,8)
        data = cpu.memory.data
        return (bytes(data[addr:addr+32]).split(b'\0')[0],
                bytes(data[addr+SOURCE:addr+SOURCE+source_len]),
                bytes(data[addr+BINARY:addr+BINARY+binary_len]))

    def test_disk_boot_reports_nonbootable_and_shutdown(self):
        cpu,out = self.firmware('help\nboot disk0\nshutdown\n')
        self.assertIn('OK> help\n',out)
        self.assertIn('DISK0 NOT BOOTABLE',out)
        self.assertIn('OK> shutdown\nShutdown.\n',out)
        self.assertEqual(cpu.memory.read(DIRECTORY,1),0)

    def test_edit_save_assemble_and_inspect_guest_bytes(self):
        cpu,out = self.firmware('edit demo.s\nstart: MOV.q #42, R0\nHALT\n.save\nasm demo.s\nfiles\ntype demo.s\nshutdown\n')
        name,source,binary = self.file(cpu)
        self.assertEqual(name,b'demo.s')
        self.assertEqual(source,b'start: MOV.q #42, R0\nHALT\n')
        self.assertEqual(binary,bytes.fromhex('03f82a00f606'))
        self.assertEqual(cpu.memory.read(DIRECTORY+48,8),0x80000)
        self.assertIn('Assembled 6 bytes in RAM; entry=524288\n',out)
        self.assertIn('demo.s source=26 binary=6\n',out)

    def test_editor_preserves_case_punctuation_and_location_counter(self):
        cpu,out = self.firmware('edit punctuation.s\n.byte \'.\', \';\'\nBRA .\n.save\nasm punctuation.s\nshutdown\n')
        self.assertEqual(self.file(cpu)[2],b'.;\xdc\xfe')
        self.assertNotIn('?ASSEMBLY ERROR',out)

    def test_labels_data_and_full_extension_assembler(self):
        source='.entry start\nstart: FMOV.d #1.0, F0\nSOB R0, start\nHALT\n'
        cpu,out=self.firmware('edit full.s\n'+source+'.save\nasm full.s\nshutdown\n')
        self.assertEqual(self.file(cpu)[2],assemble(source,origin=0x80000).code)
        self.assertIn('Assembled ',out)

    def test_basic_returns_to_ok_and_does_not_touch_files(self):
        cpu,out=self.firmware('edit keep.s\nNOP\n.save\nbasic\n10 PRINT "from BASIC"\nRUN\nBYE\nasm keep.s\nshutdown\n')
        self.assertIn('\nfrom BASIC\n',out)
        self.assertIn('> BYE\nOK> asm keep.s\n',out)
        self.assertEqual(self.file(cpu),(b'keep.s',b'NOP\n',b'\xe0'))

    def test_cancel_is_transactional_for_existing_and_new_files(self):
        cpu,out=self.firmware('edit keep.s\nNOP\n.save\nasm keep.s\nedit keep.s\nHALT\n.cancel\nedit never.s\nNOP\n.cancel\nshutdown\n')
        self.assertEqual(self.file(cpu),(b'keep.s',b'NOP\n',b'\xe0'))
        self.assertEqual(cpu.memory.read(DIRECTORY+SLOT,1),0)
        self.assertEqual(out.count('Cancelled; RAM file unchanged.\n'),2)

    def test_save_replaces_source_and_invalidates_binary(self):
        cpu,out=self.firmware('edit replace.s\nNOP\n.save\nasm replace.s\nedit replace.s\nHALT\n.save\nshutdown\n')
        self.assertEqual(self.file(cpu),(b'replace.s',b'HALT\n',b''))
        self.assertEqual(cpu.memory.read(DIRECTORY+48,8),0)

    def test_assembly_failure_and_output_capacity_recover(self):
        cpu,out=self.firmware('edit bad.s\nBOGUS R0\n.save\nasm bad.s\nedit large.s\n.space 8129\n.save\nasm large.s\nfiles\nshutdown\n')
        self.assertEqual(out.count('?ASSEMBLY ERROR:'),2)
        self.assertIn('line 1: unknown instruction',out)
        self.assertIn('output exceeds RAM file capacity',out)
        self.assertEqual(self.file(cpu)[2],b'')
        self.assertEqual(self.file(cpu,1)[2],b'')

    def test_names_arguments_unknown_commands_and_missing_files(self):
        cpu,out=self.firmware('asm missing.s\ntype missing.s\nedit ../escape\nedit '+('x'*32)+'\nshutdown extra\nboot\nwat\nEDIT case.S\nNOP\n.SAVE\nasm case.s\nasm case.S\nshutdown\n')
        self.assertEqual(out.count('?FILE NOT FOUND\n'),3)
        self.assertEqual(out.count('?NAME:'),3)
        self.assertIn('?UNEXPECTED ARGUMENTS',out)
        self.assertIn('?UNKNOWN COMMAND',out)
        self.assertEqual(self.file(cpu),(b'case.S',b'NOP\n',b'\xe0'))

    def test_directory_capacity(self):
        session=''.join(f'edit file{i}.s\nNOP\n.save\n' for i in range(8))
        cpu,out=self.firmware(session+'edit full.s\nfiles\nshutdown\n')
        self.assertIn('?RAM DIRECTORY FULL (8 files)',out)
        for i in range(8):
            self.assertEqual(self.file(cpu,i)[0],f'file{i}.s'.encode())
        self.assertEqual(cpu.memory.read(DIRECTORY+8*SLOT,1),0)

    def test_file_capacity_and_overlong_lines_are_not_committed(self):
        source_line=';'+'x'*119
        cpu,out=self.firmware('edit limit.s\n'+(source_line+'\n')*68+'.save\nasm limit.s\n'+'x'*121+'\nshutdown\n')
        self.assertIn('?FILE FULL (8191 bytes)',out)
        self.assertIn('?LINE TOO LONG (120 characters)',out)
        self.assertEqual(len(self.file(cpu)[1]),67*121)
        self.assertEqual(self.file(cpu)[2],b'')  # comments only

    def test_crlf_backspace_eof_and_ram_reset(self):
        cpu,out=self.firmware('edit a.s\r\nNOPX\x7f\r\n.save\r\n')
        self.assertEqual(self.file(cpu)[:2],(b'a.s',b'NOP\n'))
        self.assertIn('Shutdown.',out)
        cpu.reset(self.program.origin)
        # Directory is wiped by guest reset initialization, not a host filesystem.
        for _ in range(40):cpu.step()
        self.assertEqual(cpu.memory.read(DIRECTORY,1),0)

    def test_binary_cli_with_piped_keyboard(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'firmware.bin'
            path.write_bytes(self.program.code)
            result=subprocess.run([sys.executable,'-m','upsilon','run',str(path),'--binary','--boot',
                                   '--keyboard'],cwd=ROOT,
                                  input='edit cli.s\nHALT\n.save\nasm cli.s\nshutdown\n',
                                  capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('Assembled 2 bytes in RAM',result.stdout)
            self.assertIn('OK> shutdown',result.stdout)

class NativeAssemblyTests(unittest.TestCase):
    def test_no_service_path_in_firmware(self):
        source=read_native_source(ROOT/'examples/native_assembler.s')
        self.assertNotIn('SYSCALL #',source)
        self.assertNotIn('SYSCALL #',(ROOT/'examples/monitor.s').read_text())
        self.assertIn('JSR na_assemble',(ROOT/'examples/monitor.s').read_text())

    def test_syscall_keeps_architectural_behavior(self):
        cpu=CPU(Memory(65536));cpu.load(assemble('SYSCALL #0x5541'))
        with self.assertRaises(Fault) as caught:cpu.step()
        self.assertEqual(caught.exception.vector,12)
