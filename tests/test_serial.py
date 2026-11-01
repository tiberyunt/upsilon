"""Serial/ROM integration and restartable I/O side effects."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

from upsilon import CPU, Memory, SerialConsole, Fault, assemble

ROOT = Path(__file__).resolve().parents[1]

class SerialTests(unittest.TestCase):
    def cpu(self, source, input_bytes=b''):
        serial = SerialConsole(input_bytes)
        cpu = CPU(Memory(65536, serial=serial))
        cpu.load(assemble(source))
        return cpu, serial

    def test_tx_rx_and_ready_bits(self):
        cpu, serial = self.cpu('''MOV.b @0xffff0004, R0
MOV.b @0xffff0000, R1
MOV.b @0xffff0004, R2
MOV.b R1, @0xffff0000
HALT''', b'A')
        cpu.run()
        self.assertEqual(cpu.r[:3], [3, ord('A'), 2])
        self.assertEqual(serial.output, b'A')
        self.assertEqual(bytes(serial.input), b'')
        serial.feed(b'B')
        self.assertEqual(cpu.read(serial.BASE, 1), ord('B'))
        self.assertEqual(cpu.read(serial.BASE, 1), 0)

    def test_repeated_accesses_keep_order_in_one_instruction(self):
        cpu, serial = self.cpu('XCHG.b @0xffff0000, @0xffff0000\nHALT', b'AB')
        cpu.run()
        self.assertEqual(serial.output, b'BA')
        self.assertEqual(bytes(serial.input), b'')

    def test_late_fault_rolls_back_rx_and_tx(self):
        # XCHG stages a receive, then a transmit, then faults writing a literal.
        cpu, serial = self.cpu('XCHG.b @0xffff0000, #1', b'A')
        before = cpu.snapshot()
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 3)
        self.assertEqual(cpu.snapshot(), before)
        self.assertEqual(bytes(serial.input), b'A')
        self.assertEqual(serial.output, b'')

    def test_invalid_serial_accesses_are_restartable(self):
        for source in ('MOV.q #65, @0xffff0000', 'MOV.b #65, @0xffff0004',
                       'MOV.q @0xffff0000, R0'):
            cpu, serial = self.cpu(source, b'A')
            before = cpu.snapshot()
            with self.subTest(source=source), self.assertRaises(Fault):
                cpu.step()
            self.assertEqual(cpu.snapshot(), before)
            self.assertEqual(serial.output, b'')
            self.assertEqual(bytes(serial.input), b'A')
        cpu, serial = self.cpu('NOP', b'A')
        cpu.pc = serial.BASE
        with self.assertRaises(Fault):
            cpu.step()
        self.assertEqual(bytes(serial.input), b'A')

    def test_mmio_works_through_virtual_mapping(self):
        serial = SerialConsole()
        cpu = CPU(Memory(131072, serial=serial))
        program = assemble('MOV.b #65, @0x4000\nHALT')
        cpu.memory.load(0x9000, program.code)
        for address, value in [(0x1000, 0x2001), (0x2000, 0x3001), (0x3000, 0x4001),
                               (0x4008, 0x900F), (0x4020, serial.BASE | 7)]:
            cpu.memory.load(address, value.to_bytes(8, 'little'))
        cpu.sys['PTBR0'], cpu.sys['MMUCTL'], cpu.pc = 0x1000, 1, 0x1000
        cpu.run()
        self.assertEqual(serial.output, b'A')

    def test_rom_header_boot_and_greeting(self):
        serial = SerialConsole()
        cpu = CPU(Memory(serial=serial))
        program = assemble((ROOT / 'examples/hello_rom.s').read_text())
        cpu.memory.load_rom(program.origin, program.code)
        cpu.reset(program.origin)
        self.assertEqual((cpu.r[31], cpu.pc), (0x100000, program.symbols['start']))
        cpu.run(1000)
        self.assertEqual(serial.output, b'Hello from CISCland\n')
        self.assertTrue(cpu.halted)

    def test_guest_cannot_write_to_rom(self):
        cpu = CPU(Memory())
        program = assemble('MOV.b #65, (R0)+')
        cpu.memory.load_rom(program.origin, program.code)
        cpu.pc, cpu.r[0] = program.origin, program.origin
        before = bytes(cpu.memory.data)
        with self.assertRaises(Fault) as caught:
            cpu.step()
        self.assertEqual(caught.exception.vector, 8)
        self.assertEqual(cpu.r[0], program.origin)
        self.assertEqual(bytes(cpu.memory.data), before)

    def test_cli_source_and_binary_boot(self):
        source = ROOT / 'examples/hello_rom.s'
        result = subprocess.run([sys.executable, '-m', 'upsilon', 'run', str(source), '--boot', '--serial'],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'Hello from CISCland\n')
        self.assertIn('halted', result.stderr)

    def test_json_stdout_is_not_corrupted_by_serial(self):
        result = subprocess.run([sys.executable, '-m', 'upsilon', 'run', 'examples/hello_rom.s',
                                 '--boot', '--serial', '--json'], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)['halted'])
        self.assertEqual(result.stderr, 'Hello from CISCland\n')

if __name__ == '__main__':
    unittest.main()
