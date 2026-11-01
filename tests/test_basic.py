"""Boot the guest BASIC ROM and exercise keyboard/serial paths."""
from pathlib import Path
import os
import pty
import select
import subprocess
import sys
import termios
import time
import unittest

from upsilon import assemble,CPU,Memory,SerialConsole

ROOT=Path(__file__).resolve().parents[1]

class BasicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program=assemble((ROOT/'examples/basic_rom.s').read_text())

    def basic(self, text):
        serial=SerialConsole(text.encode('ascii'))
        serial.eof=True
        cpu=CPU(Memory(serial=serial))
        cpu.memory.load_rom(self.program.origin,self.program.code)
        cpu.reset(self.program.origin)
        cpu.run(200000)
        self.assertTrue(cpu.halted)
        return serial.output.decode('ascii')

    def test_print_strings_arithmetic_and_commands(self):
        out=self.basic('PRINT "Hello from CISCland"\nPRINT 10+20-3\nADD 8, 9\nSUB 8, 9\n? "answer", 3+4\nBYE\n')
        for result in ('\nHello from CISCland\n','\n27\n','\n17\n','\n-1\n','\nanswer 7\n'):
            self.assertIn(result,out)

    def test_numbered_program_goto_and_end(self):
        out=self.basic('10 PRINT "first"\n20 GOTO 40\n30 PRINT "SHOULD NOT RUN"\n40 PRINT 9-2\n50 END\nRUN\nBYE\n')
        self.assertIn('\nfirst\n',out)
        self.assertIn('\n7\n',out)
        self.assertNotIn('\nSHOULD NOT RUN\n',out)

    def test_edit_list_delete_and_new(self):
        out=self.basic('30 PRINT 30\n10 PRINT 10\n20 PRINT 20\n20 PRINT 22\n30\nLIST\nRUN\nNEW\nRUN\nBYE\n')
        self.assertIn('> LIST\n10 PRINT 10\n20 PRINT 22\n',out)
        self.assertIn('> RUN\n10\n22\n',out)
        self.assertIn('> NEW\n> RUN\n> BYE\n',out)

    def test_case_insensitivity_crlf_and_backspace(self):
        out=self.basic('print 12\x7f3\r\nprint "lower"\r\nbye\r\n')
        self.assertIn('\n13\n',out)
        self.assertIn('\nlower\n',out)
        self.assertNotIn('ERROR',out)

    def test_signed_64_bit_boundaries(self):
        out=self.basic('PRINT -9223372036854775808\nPRINT 9223372036854775807\nPRINT 9223372036854775807+1\nPRINT -9223372036854775808-1\nPRINT 9223372036854775808\nPRINT 1- -2\nBYE\n')
        self.assertIn('\n-9223372036854775808\n',out)
        self.assertIn('\n9223372036854775807\n',out)
        self.assertEqual(out.count('?NUMBER RANGE\n'),3)
        self.assertIn('\n3\n',out)

    def test_errors_recover_to_prompt(self):
        out=self.basic('GOTO 999\nPRINT nope\nPRINT 1*2\n0 PRINT 1\n1000 PRINT 1\nPRINT 6\nBYE\n')
        self.assertIn('?UNDEFINED LINE\n',out)
        self.assertEqual(out.count('?SYNTAX ERROR\n'),2)
        self.assertEqual(out.count('?LINE NUMBER 1..999\n'),2)
        self.assertIn('\n6\n',out)

    def test_long_line_rejected_and_eof_without_newline(self):
        out=self.basic('PRINT "'+'x'*121+'"\nPRINT 42')
        self.assertIn('?LINE TOO LONG\n',out)
        self.assertIn('\n42\n',out)

    def test_goto_direct_and_missing_line_stops_run(self):
        out=self.basic('10 PRINT "jumped"\n20 GOTO 999\n30 PRINT "wrong"\nGOTO 10\nPRINT 5\nBYE\n')
        self.assertIn('\njumped\n?UNDEFINED LINE\n',out)
        self.assertNotIn('\nwrong\n',out)
        self.assertIn('\n5\n',out)

    def test_piped_keyboard_eof(self):
        result=subprocess.run([sys.executable,'-m','upsilon','run','examples/basic_rom.s','--boot','--keyboard'],
                              cwd=ROOT,input='PRINT 40+2\n',capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('\n42\n',result.stdout)
        self.assertIn('halted',result.stderr)

    def test_live_tty_keyboard_and_terminal_restoration(self):
        master,slave=pty.openpty()
        old=termios.tcgetattr(slave)
        proc=subprocess.Popen([sys.executable,'-m','upsilon','run','examples/basic_rom.s','--boot','--keyboard'],
                              cwd=ROOT,stdin=slave,stdout=slave,stderr=subprocess.PIPE)
        output=bytearray()
        deadline=time.monotonic()+10
        sent=False
        try:
            while time.monotonic()<deadline:
                if select.select([master],[],[],.1)[0]:
                    output.extend(os.read(master,4096))
                if not sent and b'> ' in output:
                    os.write(master,b'PRINT 5+7\rBYE\r')
                    sent=True
                if proc.poll() is not None:
                    while select.select([master],[],[],0)[0]:
                        output.extend(os.read(master,4096))
                    break
            self.assertEqual(proc.wait(timeout=1),0)
            self.assertIn(b'\r\n12\r\n',output)
            restored=termios.tcgetattr(slave)
            # macOS sets PENDIN when canonical input is restored; it is kernel
            # bookkeeping, not a changed terminal preference.
            restored[3] &= ~getattr(termios,'PENDIN',0)
            old[3] &= ~getattr(termios,'PENDIN',0)
            self.assertEqual(restored,old)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            if proc.stderr:
                proc.stderr.close()
            os.close(master)
            os.close(slave)
