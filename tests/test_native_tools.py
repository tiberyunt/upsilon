"""Native tools must build and run in a checkout containing no simulator."""
from pathlib import Path
import json, shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class IndependentTools(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.tree=Path(cls.temp.name)/'tools';cls.tree.mkdir()
        for name in ['assembler','common']:shutil.copytree(ROOT/name,cls.tree/name)
        shutil.copyfile(ROOT/'Makefile',cls.tree/'Makefile')
        p=subprocess.run(['make','-C',str(cls.tree),'assembler','linker'],capture_output=True,text=True)
        if p.returncode:raise RuntimeError(p.stdout+p.stderr)
        cls.assembler=cls.tree/'build/upsilon-as';cls.linker=cls.tree/'build/upsilon-ld'
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def run_tool(self,*args,ok=True):
        p=subprocess.run(list(map(str,args)),capture_output=True,text=True)
        self.assertEqual(p.returncode==0,ok,p.stdout+p.stderr);return p
    def test_build_has_no_simulator_or_firmware(self):
        self.assertTrue(self.assembler.is_file());self.assertTrue(self.linker.is_file())
        self.assertFalse((self.tree/'simulator').exists())
        self.assertFalse((self.tree/'build/upsilon').exists())
        self.assertFalse((self.tree/'build/firmware_image.h').exists())
        if shutil.which('nm'):
            p=self.run_tool('nm',self.assembler)
            for symbol in ['cpu_init','cpu_step','tape_open','disk_open']:self.assertNotIn(symbol,p.stdout)
    def test_object_link_and_execute(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);a=d/'a.s';b=d/'b.s';aobj=d/'a.obj';bobj=d/'b.obj';out=d/'out.bin';m=d/'map.json'
            a.write_text('.extern helper\n.entry main\nmain: JSR helper\nHALT\n')
            b.write_text('.globl helper\nhelper: MOV.q #42,R0\nRET\n')
            self.run_tool(self.assembler,'-c',a,'-o',aobj)
            self.run_tool(self.assembler,'-c',b,'-o',bobj)
            self.assertEqual(aobj.read_bytes()[:8],b'UOBJ1\0\0\0')
            self.run_tool(self.linker,aobj,bobj,'-o',out,'--map',m)
            self.assertEqual(json.loads(m.read_text())['symbols']['main'],4096)
            p=self.run_tool(ROOT/'build/upsilon','run',out,'--binary','--json')
            self.assertEqual(json.loads(p.stdout)['registers']['R0'],42)
            expected=out.read_bytes()
            self.run_tool(self.linker,aobj,b,'-o',out)
            self.assertEqual(out.read_bytes(),expected)
    def test_unresolved_symbol_preserves_output(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);s=d/'a.s';o=d/'a.obj';out=d/'out.bin'
            s.write_text('.extern missing\nJSR missing\nHALT\n');out.write_bytes(b'keep')
            self.run_tool(self.assembler,'-c',s,'-o',o)
            self.run_tool(self.linker,o,'-o',out,ok=False)
            self.assertEqual(out.read_bytes(),b'keep')
    def test_bad_object_and_duplicate_entries_preserve_output(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);bad=d/'bad.obj';a=d/'a.s';b=d/'b.s';out=d/'out.bin'
            out.write_bytes(b'keep');bad.write_bytes(b'UOBJ1\0\0\0')
            self.run_tool(self.linker,bad,'-o',out,ok=False)
            a.write_text('.entry first\nfirst: HALT\n');b.write_text('.entry second\nsecond: HALT\n')
            self.run_tool(self.linker,a,b,'-o',out,ok=False)
            self.assertEqual(out.read_bytes(),b'keep')
    def test_object_includes_are_resolved_when_assembled(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);s=d/'a.s';inc=d/'inc.s';obj=d/'a.obj';out=d/'out.bin'
            inc.write_text('MOV.q #7,R0\n');s.write_text('.include "inc.s"\nHALT\n')
            self.run_tool(self.assembler,'-c',s,'-o',obj);inc.unlink()
            self.run_tool(self.linker,obj,'-o',out)
            self.assertTrue(out.read_bytes().endswith(bytes.fromhex('f606')))
if __name__=='__main__':unittest.main(verbosity=2)
