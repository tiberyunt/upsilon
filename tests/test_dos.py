"""Native tape DOS: independent boots, durable files, editor and guest tools."""
import json
import os
from pathlib import Path
import re
import pty
import select
import termios
import time
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('UPSILON_C',ROOT/'build/upsilon')).resolve()
INSTALL=ROOT/'build/dos-install.tap'
ANSI=re.compile(r'\x1b\[[0-9;]*[A-Za-z]')


def records(image):
    at=0
    while at<len(image):
        n=struct.unpack_from('<I',image,at)[0];at+=4
        if n==0xffffffff:return
        if n in (0,0xfffffffe):continue
        size=n&0xffffff
        data=image[at:at+size];at+=(size+1)&~1
        assert struct.unpack_from('<I',image,at)[0]==n
        at+=4
        yield data


def journal(reel):
    files={};seq=0
    for data in records(reel.read_bytes()):
        if not data.startswith(b'UPSDOS1\0'):continue
        sequence,op=struct.unpack_from('<QQ',data,8)
        assert sequence==seq+1
        checksum=struct.unpack_from('<Q',data,176)[0]
        checked=bytearray(data);checked[176:184]=b'\0'*8
        h=14695981039346656037
        for b in checked:h=((h^b)*1099511628211)&((1<<64)-1)
        assert h==checksum
        name=data[24:88].split(b'\0')[0].decode()
        length,kind,entry=struct.unpack_from('<QQQ',data,152)
        assert len(data)==192+length
        if op==1:files[name]=(data[192:],kind,entry)
        elif op==2:files.pop(name,None)
        elif op==3:
            new=data[88:152].split(b'\0')[0].decode()
            if name in files:files[new]=files.pop(name)
        else:raise AssertionError(op)
        seq=sequence
    return files,seq


class DosTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.reel=Path(self.temp.name)/'dos.tap'
        shutil.copyfile(INSTALL,self.reel)

    def boot(self,commands,extra=(),reel=None):
        result=subprocess.run([str(EXE),'-m','16','-t',str(reel or self.reel),
                               '--max-steps','50000000',*map(str,extra)],
                              input=commands,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn('Program fault',result.stdout)
        self.assertNotIn('journal damaged',result.stdout)
        return ANSI.sub('',result.stdout)

    def test_boot_files_autoexec_and_no_host_services(self):
        before=self.reel.read_bytes()
        out=self.boot('dir\nhelp\nshutdown\n')
        for name in ('IO.SYS','COMMAND.EXE','AUTOEXEC.BAT','VI.EXE','ASM.EXE','LINK.EXE'):
            self.assertIn(name,out)
        self.assertIn('KotlTOS is ready.',out)
        self.assertEqual(self.reel.read_bytes(),before)
        self.assertRegex((ROOT/'dos/tools.inc').read_text(),r'(?:JSR|JMP) na_assemble')
        self.assertNotIn('assemble(', (ROOT/'simulator/cpu.c').read_text())
        self.assertNotIn('UPSDOS', (ROOT/'simulator/cpu.c').read_text())

    def test_file_operations_survive_two_independent_boots(self):
        out=self.boot('echo first > a.txt\necho second >> a.txt\ncopy a.txt b.txt\nren b.txt c.txt\ndel a.txt\n'
                      'md docs\ncd docs\necho nested > sub.txt\ncd ..\nshutdown\n')
        self.assertNotIn('KotlTOS error',out)
        files,seq=journal(self.reel)
        self.assertEqual(files['A:\\C.TXT'][0],b'first\nsecond\n')
        self.assertEqual(files['A:\\DOCS\\SUB.TXT'][0],b'nested\n')
        self.assertNotIn('A:\\A.TXT',files)
        self.assertNotIn('A:\\B.TXT',files)
        out=self.boot('type c.txt\ncd docs\ntype sub.txt\ncd ..\ndir\nshutdown\n')
        self.assertIn('first\nsecond\n',out)
        self.assertIn('nested\n',out)
        self.assertEqual(journal(self.reel)[1],seq) # reads do not append

    def test_paths_directories_wildcards_and_reused_slots(self):
        out=self.boot('md work\ncd work\necho x > one.txt\necho y > two.txt\necho z > keep.asm\n'
                      'dir *.txt\ndel t??.txt\ncd ..\nrd work\ncd work\ndel *.txt\ndel keep.asm\n'
                      'cd ..\nrd work\nmd again\ncd again\necho yes > okay.txt\n'
                      'cd ..\\..\\..\ntype a:\\again\\okay.txt\nshutdown\n')
        self.assertIn('ONE.TXT',out);self.assertIn('TWO.TXT',out)
        self.assertEqual(out.count('KotlTOS error'),1) # first RD refuses nonempty dir
        files,_=journal(self.reel)
        self.assertNotIn('A:\\WORK',files)
        self.assertEqual(files['A:\\AGAIN\\OKAY.TXT'][0],b'yes\n')

    def test_bad_names_arguments_and_unknown_commands_recover(self):
        out=self.boot('copy\ndel\nren x\nmd\ncd missing\nnotreal\n'
                      'echo no > toolongname.txt\necho no > \\\n'
                      'echo valid > valid.txt\ntype valid.txt\nshutdown\n')
        self.assertGreaterEqual(out.count('Invalid or missing command arguments'),4)
        self.assertIn('valid\n',out)
        files,_=journal(self.reel)
        self.assertEqual(len(files),13)
        self.assertIn('A:\\VALID.TXT',files)

    def test_vi_save_undo_line_delete_and_quit_protection(self):
        self.boot('vi note.txt\niOne\nTwo\nThree\x1bggddu:w\n'
                  'ix\x1b:q\n:q!\ntype note.txt\nshutdown\n')
        files,_=journal(self.reel)
        self.assertEqual(files['A:\\NOTE.TXT'][0],b'One\nTwo\nThree')
        out=self.boot('type note.txt\nshutdown\n')
        self.assertIn('One\nTwo\nThree',out)

    def test_vi_save_as_and_arrow_navigation(self):
        self.boot('edit a.txt\niabc\x1b[Dhix\x1b:w b.txt\n:q\nshutdown\n')
        files,_=journal(self.reel)
        self.assertIn('A:\\B.TXT',files)
        self.assertNotIn('A:\\A.TXT',files)
        self.assertIn(b'x',files['A:\\B.TXT'][0])

    def test_autoexec_is_an_editable_persistent_file(self):
        self.boot('vi autoexec.bat\nggdd dd dd i@echo off\necho CUSTOM STARTUP\n\x1b:wq\nshutdown\n')
        out=self.boot('shutdown\n')
        self.assertIn('CUSTOM STARTUP',out)
        self.assertNotIn('KotlTOS is ready. Files',out)

    def test_native_assemble_link_execute_and_reboot(self):
        out=self.boot('asm hello.asm\nlink hello.exe hello.obj\nhello\n'
                      'asm main.asm main.obj\nasm greet.asm greet.obj\n'
                      'link both.exe main.obj greet.obj\nboth\nshutdown\n')
        self.assertNotIn('KotlTOS error',out)
        self.assertIn('Hello from KotlTOS!\n',out)
        self.assertIn('Linked two native modules.\n',out)
        files,_=journal(self.reel)
        obj=files['A:\\HELLO.OBJ']
        source_len,code_len,entry=struct.unpack_from('<QQQ',obj[0],8)
        self.assertEqual(obj[1],2)
        self.assertGreater(code_len,0)
        self.assertEqual(len(obj[0]),32+source_len+code_len)
        self.assertEqual(entry,0x200000)
        self.assertEqual(files['A:\\HELLO.EXE'][1:],(1,0x200000))
        out=self.boot('hello\nboth\nshutdown\n')
        self.assertIn('Hello from KotlTOS!\n',out)
        self.assertIn('Linked two native modules.\n',out)

    def test_link_error_preserves_previous_executable(self):
        self.boot('asm main.asm\nasm greet.asm\nlink both.exe main.obj greet.obj\nshutdown\n')
        old=journal(self.reel)[0]['A:\\BOTH.EXE']
        out=self.boot('link both.exe main.obj\nboth\nshutdown\n')
        self.assertIn('undefined symbol',out)
        self.assertIn('Linked two native modules.',out)
        self.assertEqual(journal(self.reel)[0]['A:\\BOTH.EXE'],old)

    def test_native_application_fault_returns_to_command(self):
        # Faults are expected in this test; inspect output without the helper's no-fault assertion.
        result=subprocess.run([str(EXE),'-m','16','-t',str(self.reel)],
            input='echo DIV.q #0,R0 > bad.asm\nasm bad.asm\nlink bad.exe bad.obj\nbad\n'
                  'echo alive > alive.txt\nshutdown\n',text=True,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('Program fault, vector 9',result.stdout)
        self.assertIn('KotlTOS halted.',result.stdout)
        self.assertEqual(journal(self.reel)[0]['A:\\ALIVE.TXT'][0],b'alive\n')

    def test_nested_batch_files_and_echo(self):
        out=self.boot('echo echo INNER > inner.bat\necho call inner.bat > outer.bat\n'
                      'echo echo OUTER >> outer.bat\ncall outer.bat\nshutdown\n')
        self.assertIn('INNER\n',out);self.assertIn('OUTER\n',out)
        self.assertNotIn('KotlTOS error',out)

    def test_readonly_tape_rejects_writes_without_changing_files(self):
        before=self.reel.read_bytes()
        out=self.boot('echo fail > nope.txt\ndel hello.asm\nvi hello.asm\niX\x1b:w\n:q!\nshutdown\n',
                      extra=('--tape-readonly',))
        self.assertIn('write-protected',out)
        self.assertEqual(self.reel.read_bytes(),before)

    def test_full_directory_does_not_commit_failed_create(self):
        out=self.boot(''.join(f'echo {i} > f{i}.txt\n' for i in range(55))+'shutdown\n')
        files,seq=journal(self.reel)
        self.assertEqual(len(files),64)
        self.assertIn('KotlTOS error',out)
        self.assertNotIn('A:\\F54.TXT',files)
        self.boot('del f0.txt\necho reuse > reused.txt\nshutdown\n')
        self.assertEqual(journal(self.reel)[0]['A:\\REUSED.TXT'][0],b'reuse\n')

    def test_journal_corruption_recovers_previous_files_readonly(self):
        self.boot('echo safe > safe.txt\necho broken > broken.txt\nshutdown\n')
        image=bytearray(self.reel.read_bytes())
        marker=image.rfind(b'UPSDOS1\0')
        self.assertGreater(marker,0)
        image[marker+192]^=1
        self.reel.write_bytes(image)
        result=subprocess.run([str(EXE),'-m','16','-t',str(self.reel)],
                              input='type safe.txt\necho nope > nope.txt\nshutdown\n',
                              capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('recovered files mounted read-only',result.stdout)
        self.assertIn('safe\n',result.stdout)
        self.assertIn('KotlTOS error',result.stdout)
        self.assertEqual(self.reel.read_bytes(),image)

    def test_io_sys_is_loaded_from_latest_filesystem_version(self):
        self.boot('copy io.sys backup.sys\ndel io.sys\ncopy backup.sys io.sys\nshutdown\n')
        out=self.boot('ver\nshutdown\n')
        self.assertIn('KotlTOS 0.2',out)
        self.assertEqual(journal(self.reel)[0]['A:\\IO.SYS'][0],(ROOT/'build/IO.SYS').read_bytes())

    def test_second_process_cannot_write_a_mounted_reel(self):
        first=subprocess.Popen([str(EXE),'-m','16','-t',str(self.reel)],
                               stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            out=bytearray();deadline=time.monotonic()+10
            while b'A:\\> ' not in out and time.monotonic()<deadline:
                if select.select([first.stdout],[],[],.1)[0]:out.extend(os.read(first.stdout.fileno(),4096))
            self.assertIn(b'A:\\> ',out)
            before=self.reel.read_bytes()
            second=self.boot('echo conflict > conflict.txt\nshutdown\n')
            self.assertIn('write-protected',second)
            self.assertEqual(self.reel.read_bytes(),before)
            rest,error=first.communicate(b'echo owner > owner.txt\nshutdown\n',timeout=10)
            self.assertEqual(first.returncode,0,error.decode())
            self.assertNotIn(b'KotlTOS error',rest)
            self.assertEqual(journal(self.reel)[0]['A:\\OWNER.TXT'][0],b'owner\n')
        finally:
            if first.poll() is None:first.kill();first.wait()
            for stream in (first.stdin,first.stdout,first.stderr):
                if stream:stream.close()

    def test_real_terminal_editor_and_terminal_restoration(self):
        master,slave=pty.openpty();old=termios.tcgetattr(slave)
        process=subprocess.Popen([str(EXE),'-m','16','-t',str(self.reel)],
                                 stdin=slave,stdout=slave,stderr=subprocess.PIPE)
        output=bytearray();phase=0;checkpoint=0;deadline=time.monotonic()+15
        try:
            while time.monotonic()<deadline:
                if select.select([master],[],[],.1)[0]:output.extend(os.read(master,4096))
                if phase==0 and b'A:\\> ' in output:
                    checkpoint=len(output);os.write(master,b'vi tty.txt\r');phase=1
                elif phase==1 and b'-- NORMAL -- A:\\TTY.TXT' in output[checkpoint:]:
                    checkpoint=len(output);os.write(master,b'iFrom terminal\r\x1b:wq\r');phase=2
                elif phase==2 and b'A:\\> ' in output[checkpoint:]:
                    os.write(master,b'type tty.txt\rshutdown\r');phase=3
                if process.poll() is not None:break
            self.assertEqual(process.wait(timeout=1),0)
            self.assertEqual(phase,3)
            self.assertIn(b'From terminal',output)
            restored=termios.tcgetattr(slave)
            old[3]&=~getattr(termios,'PENDIN',0);restored[3]&=~getattr(termios,'PENDIN',0)
            self.assertEqual(restored,old)
            self.assertEqual(journal(self.reel)[0]['A:\\TTY.TXT'][0],b'From terminal\n')
        finally:
            if process.poll() is None:process.kill();process.wait()
            if process.stderr:process.stderr.close()
            os.close(master);os.close(slave)

    def test_packager_rejects_bad_names_without_overwriting_reel(self):
        before=self.reel.read_bytes()
        for additions in [('--add','TOOLONG123.TXT='+str(ROOT/'dos/HELLO.ASM')),
                          ('--add','ONE.TXT='+str(ROOT/'dos/HELLO.ASM'),
                           '--add','one.txt='+str(ROOT/'dos/HELLO.ASM'))]:
            result=subprocess.run([str(EXE),'mktape',str(ROOT/'build/dos_boot.bin'),
                                   '-o',str(self.reel),'--load','0x80000',*additions],
                                  capture_output=True,text=True,timeout=10)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(self.reel.read_bytes(),before)
