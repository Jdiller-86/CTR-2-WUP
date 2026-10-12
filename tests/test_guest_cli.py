import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from host.guest_fixture import create,fixture_bytes
from host.guest_pipeline import run_fixture
from host.errors import HostError


@unittest.skipUnless(shutil.which("gcc"),"desktop fixture integration requires native GCC")
class GuestCliTests(unittest.TestCase):
    def test_aot_and_interpreter_real_pipeline(self):
        with tempfile.TemporaryDirectory(prefix="guest Unicode 日本語 ") as temporary:
            root=Path(temporary); source=root/"original fixture.cxi"; source.write_bytes(fixture_bytes())
            try:
                aot=run_fixture(source,root/"AOT output",frames=2,buttons=1,touch=True,x=100,y=80)
                interpreted=run_fixture(source,root/"interpreter output",frames=2,buttons=1,touch=True,x=100,y=80,translated=False)
            except HostError as error:
                log = error.details.get("log")
                evidence = Path(log).read_text(encoding="utf-8", errors="replace") if log else ""
                self.fail(f"Original fixture pipeline failed: {error}\n{evidence}")
            self.assertTrue(aot["ok"] and interpreted["ok"])
            self.assertEqual(aot["guest"]["instructions"],interpreted["guest"]["instructions"])
            self.assertEqual(aot["guest"]["fallback_instructions"],0)
            for name in ("top.ppm","bottom.ppm"):
                self.assertEqual((root/"AOT output"/name).read_bytes(),(root/"interpreter output"/name).read_bytes())
            pixels=(root/"AOT output/bottom.ppm").read_bytes().split(b"\n",3)[3]
            offset=(80*320+100)*3
            self.assertEqual(pixels[offset:offset+3],b"\xff\xff\xff")
            self.assertEqual(aot["guest"]["exit_state"],"exited")

    def test_compiler_failure_keeps_logs_and_no_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); source=root/"fixture.cxi"; source.write_bytes(fixture_bytes())
            with patch("host.guest_pipeline._run_build",side_effect=HostError("compile_failed","negative test",stage="compile")):
                with self.assertRaises(HostError): run_fixture(source,root/"output")
            self.assertFalse((root/"output").exists())
            self.assertFalse(list(root.glob(".ctr2wup-guest-*")))

    def test_cli_guest_input_failure_json(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); source=root/"fixture.cxi"; source.write_bytes(fixture_bytes())
            process=subprocess.run([sys.executable,"-m","host","run-fixture",str(source),"--output",str(root/"out"),"--touch-x","320"],capture_output=True,text=True)
            self.assertEqual(process.returncode,2)
            self.assertEqual(json.loads(process.stderr)["error"]["code"],"invalid_fixture_input")
            self.assertFalse((root/"out").exists())


class FixtureCreationTests(unittest.TestCase):
    def test_original_fixture_and_preserve_existing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)/"original"; result=create(root)
            self.assertTrue(result["ok"])
            self.assertEqual((root/"ctr2wup-guest.cxi").read_bytes(),fixture_bytes())
            with self.assertRaises(HostError): create(root)
            self.assertEqual((root/"ctr2wup-guest.cxi").read_bytes(),fixture_bytes())


if __name__=="__main__": unittest.main()
