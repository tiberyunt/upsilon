"""KotlTOS disk installation, qcow2 interoperability and interruptible startup."""
import hashlib
import os
from pathlib import Path
import pty
import select
import shutil
import struct
import subprocess
import tempfile
import termios
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
EXE=Path(os.environ.get('UPSILON_C',ROOT/'build/upsilon')).resolve()
QEMU=shutil.which('qemu-img')


def digest(path):return hashlib.sha256(Path(path).read_bytes()).digest()


class DiskTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.directory=Path(self.temp.name)
        self.reel=self.directory/'kotltos.tap';shutil.copyfile(ROOT/'build/dos-install.tap',self.reel)
        self.disk=self.directory/'disk.qcow2'
        self.command('mkdisk',self.disk,'--size','8')

    def command(self,*args):
        result=subprocess.run([str(EXE),*map(str,args)],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)
        return result

    def boot(self,session,tape=True,disk=None,options=(),force_tape=False):
        args=[str(EXE),'-m','16','-d',str(disk or self.disk),'--max-steps','100000000']
        if tape:args+=['-t',str(self.reel)]
        result=subprocess.run([*args,*options],input=('Aboot tape0\n' if force_tape else '')+session,
                              capture_output=True,text=True,timeout=45)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn('Program fault',result.stdout)
        return result.stdout

    def install(self):
        out=self.boot('format c:\nsyscopy c:\nshutdown\n')
        self.assertIn('C: is bootable.',out)
        return out

    def check_qcow(self,path=None):
        if QEMU:
            result=subprocess.run([QEMU,'check',str(path or self.disk)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_create_real_sparse_qcow2_and_preserve_existing_file(self):
        self.assertEqual(self.disk.read_bytes()[:4],b'QFI\xfb')
        self.assertLess(self.disk.stat().st_size,1024*1024)
        before=digest(self.disk)
        result=subprocess.run([str(EXE),'mkdisk',str(self.disk)],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertEqual(digest(self.disk),before)
        self.check_qcow()

    def test_format_c_preserves_a_and_persists_disk_files(self):
        out=self.boot('echo tape > keep.txt\nformat (C)\nmd c:\\docs\n'
                      'echo disk > c:\\docs\\note.txt\ncopy keep.txt c:\\copy.txt\nc:\n'
                      'dir\ncd docs\ntype note.txt\na:\nshutdown\n')
        self.assertNotIn('KotlTOS error',out)
        self.assertIn('C:\\>',out);self.assertIn('disk\n',out)
        out=self.boot('type keep.txt\ntype c:\\copy.txt\ntype c:\\docs\\note.txt\nshutdown\n')
        self.assertIn('tape\n',out);self.assertIn('disk\n',out)
        self.check_qcow()

    def test_syscopy_boots_without_tape_and_runs_native_tools(self):
        self.install()
        self.boot('copy a:\\hello.asm c:\\hello.asm\nshutdown\n',force_tape=True)
        out=self.boot('asm hello.asm\nlink hello.exe hello.obj\nhello\n'
                      'echo saved > persist.txt\nshutdown\n',tape=False)
        self.assertIn('Booting disk0...',out)
        self.assertIn('KotlTOS 0.2',out)
        self.assertIn('Hello from KotlTOS!',out)
        self.assertNotIn('KotlTOS error',out)
        out=self.boot('type persist.txt\nshutdown\n',tape=False)
        self.assertIn('saved\n',out);self.check_qcow()

    def test_editor_and_autoexec_use_c_after_disk_boot(self):
        self.install()
        out=self.boot('vi autoexec.bat\nggdd dd dd i@echo off\necho C DISK STARTUP\n\x1b:wq\n'
                      'md docs\ncd docs\nvi note.txt\niDisk editor file\x1b:wq\n'
                      'ren note.txt renamed.txt\nshutdown\n',tape=False)
        self.assertNotIn('KotlTOS error',out)
        out=self.boot('type c:\\docs\\renamed.txt\nshutdown\n',tape=False)
        self.assertIn('C DISK STARTUP',out)
        self.assertIn('Disk editor file',out)
        self.assertNotIn('KotlTOS is ready.',out)
        self.check_qcow()

    def test_syscopy_requires_format_and_preflights_system_files(self):
        before=digest(self.disk)
        out=self.boot('syscopy c:\nshutdown\n')
        self.assertIn('Use FORMAT C:',out);self.assertEqual(digest(self.disk),before)
        self.boot('format c:\ndel boot.sys\nshutdown\n')
        before=digest(self.disk)
        out=self.boot('syscopy a: c:\nshutdown\n')
        self.assertIn('File not found',out);self.assertEqual(digest(self.disk),before)

    def test_format_erases_only_c_and_clears_bootability(self):
        self.install()
        self.boot('echo tape > keep.txt\necho disk > c:\\gone.txt\nformat c:\n'
                  'type keep.txt\ntype c:\\gone.txt\nshutdown\n',force_tape=True)
        out=self.boot('shutdown\n',tape=False)
        self.assertIn('NO BOOTABLE DISK',out)
        self.assertNotIn('KotlTOS 0.2',out)
        out=self.boot('type keep.txt\nshutdown\n')
        self.assertIn('tape\n',out);self.check_qcow()

    def test_cross_drive_copy_and_rename_restriction(self):
        out=self.boot('format c:\necho alpha > alpha.txt\ncopy alpha.txt c:\\beta.txt\n'
                      'ren alpha.txt c:\\moved.txt\ncopy c:\\beta.txt back.txt\n'
                      'type back.txt\nshutdown\n')
        self.assertIn('Operation not allowed',out);self.assertIn('alpha\n',out)
        out=self.boot('type alpha.txt\ntype c:\\beta.txt\nshutdown\n')
        self.assertGreaterEqual(out.count('alpha\n'),2)

    def test_boot_disk1_selects_and_writes_only_the_numbered_disk(self):
        self.install()
        second=self.directory/'second.qcow2';shutil.copyfile(self.disk,second)
        self.boot('echo SECOND > identity.txt\nshutdown\n',disk=second,tape=False)
        first_before=digest(self.disk)
        result=subprocess.run([str(EXE),'-m','16','-d',str(self.disk),'-d',str(second)],
                              input='Aboot disk1\ntype identity.txt\necho selected > result.txt\nshutdown\n',
                              capture_output=True,text=True,timeout=45)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('Booting disk1...',result.stdout)
        self.assertIn('SECOND\n',result.stdout)
        self.assertNotIn('KotlTOS error',result.stdout)
        self.assertEqual(digest(self.disk),first_before)
        self.assertIn('selected\n',self.boot('type result.txt\nshutdown\n',disk=second,tape=False))
        self.check_qcow(second)

    def test_autoboot_falls_through_blank_disk0_to_disk1(self):
        self.install()
        blank=self.directory/'blank.qcow2';self.command('mkdisk',blank,'--size','8')
        result=subprocess.run([str(EXE),'-m','16','-d',str(blank),'-d',str(self.disk)],
                              input='echo unit1 > test.txt\nshutdown\n',capture_output=True,text=True,timeout=45)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('Booting disk1...',result.stdout)
        self.assertNotIn('KotlTOS error',result.stdout)
        self.assertIn('unit1\n',self.boot('type test.txt\nshutdown\n',tape=False))
        self.check_qcow()

    def test_readonly_disk_boots_but_rejects_format_and_writes(self):
        self.install();before=digest(self.disk)
        out=self.boot('echo fail > fail.txt\nformat c:\nsyscopy c:\nshutdown\n',
                      tape=False,options=('--disk-readonly',))
        self.assertIn('Booting disk0...',out)
        self.assertIn('write-protected',out);self.assertEqual(digest(self.disk),before)

    def test_press_a_enters_ok_then_boot_disk0_manually(self):
        self.install()
        out=self.boot('help\nboot disk0\nshutdown\n',tape=False,options=('--serial-input','A'))
        self.assertIn('OK> help',out)
        self.assertIn('OK> boot disk0',out)
        self.assertIn('Booting disk0...',out)

    def test_boot_uses_device_numbers_not_dos_drive_letters(self):
        out=self.boot('boot disk9\nboot C:\nhelp\nshutdown\n',options=('--serial-input','A'))
        self.assertIn('use boot disk0 through boot disk3',out)
        self.assertIn('unknown boot device',out)
        self.assertIn('boot diskN or tape0',out)
        self.assertNotIn('KotlTOS 0.2',out)

    def test_other_startup_input_is_preserved(self):
        out=self.boot('echo FIRSTCHAR\nshutdown\n')
        self.assertIn('A:\\> echo FIRSTCHAR',out)
        self.assertIn('FIRSTCHAR\n',out);self.assertNotIn('KotlTOS error',out)

    def test_real_keyboard_a_before_autoboot_and_terminal_restoration(self):
        master,slave=pty.openpty();old=termios.tcgetattr(slave)
        process=subprocess.Popen([str(EXE),'-m','16','-t',str(self.reel)],
                                 stdin=slave,stdout=slave,stderr=subprocess.PIPE)
        out=bytearray();phase=0;deadline=time.monotonic()+12
        try:
            while time.monotonic()<deadline:
                if select.select([master],[],[],.1)[0]:out.extend(os.read(master,4096))
                if phase==0 and b'Press A within 2 seconds' in out:
                    os.write(master,b'a');phase=1
                if phase==1 and b'OK> ' in out:
                    os.write(master,b'help\rshutdown\r');phase=2
                if process.poll() is not None:break
            self.assertEqual(process.wait(timeout=1),0)
            self.assertEqual(phase,2);self.assertNotIn(b'KotlTOS 0.2',out)
            restored=termios.tcgetattr(slave)
            old[3]&=~getattr(termios,'PENDIN',0);restored[3]&=~getattr(termios,'PENDIN',0)
            self.assertEqual(old,restored)
        finally:
            if process.poll() is None:process.kill();process.wait()
            process.stderr.close();os.close(master);os.close(slave)

    def test_disk_writer_lock_gives_second_machine_readonly_snapshot(self):
        self.install()
        first=subprocess.Popen([str(EXE),'-m','16','-d',str(self.disk)],
                               stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        out=bytearray();deadline=time.monotonic()+12
        try:
            while b'C:\\> ' not in out and time.monotonic()<deadline:
                if select.select([first.stdout],[],[],.1)[0]:out.extend(os.read(first.stdout.fileno(),4096))
            self.assertIn(b'C:\\> ',out)
            before=digest(self.disk)
            result=self.boot('echo fail > fail.txt\nshutdown\n',tape=False)
            self.assertIn('write-protected',result);self.assertEqual(digest(self.disk),before)
            _,err=first.communicate(b'echo owner > owner.txt\nshutdown\n',timeout=20)
            self.assertEqual(first.returncode,0,err.decode())
            self.assertIn('owner\n',self.boot('type owner.txt\nshutdown\n',tape=False))
        finally:
            if first.poll() is None:first.kill();first.wait()
            for stream in (first.stdin,first.stdout,first.stderr):stream.close()

    @unittest.skipUnless(QEMU,'optional independent QEMU interoperability checks')
    def test_qemu_created_v2_v3_images_round_trip_and_check_clean(self):
        self.install()
        for version,cluster in [('0.10','4096'),('1.1','65536')]:
            converted=self.directory/('qemu-'+version+'.qcow2')
            result=subprocess.run([QEMU,'convert','-O','qcow2','-o',f'compat={version},cluster_size={cluster}',
                                   str(self.disk),str(converted)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            out=self.boot('echo roundtrip > check.txt\nshutdown\n',disk=converted,tape=False)
            self.assertNotIn('KotlTOS error',out)
            self.assertIn('roundtrip\n',self.boot('type check.txt\nshutdown\n',disk=converted,tape=False))
            self.check_qcow(converted)

    @unittest.skipUnless(QEMU,'optional independent QEMU interoperability checks')
    def test_unsupported_backing_or_compression_rejected_without_modification(self):
        self.install()
        backing=self.directory/'backing.qcow2';compressed=self.directory/'compressed.qcow2'
        for args in [[QEMU,'create','-f','qcow2','-F','qcow2','-b',str(self.disk),str(backing)],
                     [QEMU,'convert','-c','-O','qcow2',str(self.disk),str(compressed)]]:
            subprocess.run(args,check=True,capture_output=True)
        for disk in (backing,compressed):
            before=digest(disk)
            result=subprocess.run([str(EXE),'-m','16','-d',str(disk)],input='shutdown\n',capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0);self.assertIn('unsupported qcow2',result.stderr)
            self.assertEqual(digest(disk),before)
