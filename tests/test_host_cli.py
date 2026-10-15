import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from host import build
from host.doctor import doctor
from host.errors import HostError
from host.atomic import publish_directory
from tests.host_fixtures import ncch


class CliTests(unittest.TestCase):
    def test_audio_stream_check_rejects_mixed_profiles_before_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'stream-output'
            for options in ({'guest_path': 'owned-game.cxi'}, {'backend': 'upstream'}, {'audio_only': True}, {'scene_only': True}):
                with self.subTest(options=options), patch.object(build, 'doctor') as probe:
                    with self.assertRaises(HostError) as caught:
                        build.prototype(output, stream_audio=True, **options)
                    self.assertEqual(caught.exception.code, 'invalid_profile')
                    probe.assert_not_called()
                    self.assertFalse(output.exists())

    def test_scene_check_rejects_other_profiles_before_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'scene-output'
            for options in ({'guest_path': 'owned-game.cxi'}, {'backend': 'upstream'}, {'audio_only': True}):
                with self.subTest(options=options), patch.object(build, 'doctor') as probe:
                    with self.assertRaises(HostError) as caught:
                        build.prototype(output, scene_only=True, **options)
                    self.assertEqual(caught.exception.code, 'invalid_profile')
                    probe.assert_not_called()
                    self.assertFalse(output.exists())

    def test_audio_check_rejects_guest_or_external_backend_before_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'audio-output'
            for options in ({'guest_path': 'owned-game.cxi'}, {'backend': 'upstream'}):
                with self.subTest(options=options), patch.object(build, 'doctor') as probe:
                    with self.assertRaises(HostError) as caught:
                        build.prototype(output, audio_only=True, **options)
                    self.assertEqual(caught.exception.code, 'invalid_profile')
                    probe.assert_not_called()
                    self.assertFalse(output.exists())

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.rom = self.root / "original fixture 日本語.cxi"
        self.rom.write_bytes(ncch())

    def tearDown(self):
        self.tmp.cleanup()

    def command(self, *args):
        return subprocess.run([sys.executable, "-m", "host", *map(str, args)], capture_output=True, text=True, check=False)

    def test_inspect_cli_unicode(self):
        result = self.command("inspect", self.rom)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)["ok"])

    def test_commercial_gate_leaves_no_output(self):
        output = self.root / "never published"
        result = self.command("build", self.rom, "--output", output)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stderr)["error"]["code"], "unsupported_profile")
        self.assertFalse(output.exists())

    def test_usage_errors_json(self):
        result = self.command("build", "--target", "prototype")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stderr)["error"]["code"], "usage")

    def test_inspect_failure_json(self):
        self.rom.write_bytes(b"broken")
        result = self.command("inspect", self.rom)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stderr)["error"]["code"], "truncated_header")

    def test_existing_output_preserved(self):
        existing = self.root / "existing"
        existing.mkdir()
        marker = existing / "user-file"
        marker.write_text("preserve")
        with self.assertRaises(HostError) as ctx:
            build.prototype(existing)
        self.assertEqual(ctx.exception.code, "output_exists")
        self.assertEqual(marker.read_text(), "preserve")

    def test_compiler_failure_cleans_staging(self):
        output = self.root / "failed build 日本語"
        with patch.object(build, "doctor", return_value={"ok": True}), \
             patch.object(build, "_run_build", side_effect=HostError("compile_failed", "intentional negative test", stage="compile")):
            with self.assertRaises(HostError):
                build.prototype(output)
        self.assertFalse(output.exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-staging-*")))

    def test_cancellation_cleans_staging(self):
        output = self.root / "cancelled"
        with patch.object(build, "doctor", return_value={"ok": True}), \
             patch.object(build, "_run_build", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                build.prototype(output)
        self.assertFalse(output.exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-staging-*")))

    def test_doctor_reports_missing_requirements(self):
        result = doctor({"PATH": str(self.root / "empty"), "DEVKITPRO": str(self.root / "missing-sdk")})
        self.assertFalse(result["ok"])
        self.assertIn("powerpc-eabi-gcc", result["missing_required"])
        self.assertIn("WUT headers/library", result["missing_required"])

    def test_atomic_publish_preserves_empty_destination(self):
        staging = self.root / "staging"
        output = self.root / "existing empty directory"
        staging.mkdir()
        (staging / "complete").write_text("finished")
        output.mkdir()
        with self.assertRaises(HostError) as ctx:
            publish_directory(staging, output)
        self.assertEqual(ctx.exception.code, "output_exists")
        self.assertTrue((staging / "complete").exists())
        self.assertEqual(list(output.iterdir()), [])

    def test_atomic_publish_complete_directory(self):
        staging = self.root / "staging"
        output = self.root / "new output"
        staging.mkdir()
        (staging / "complete").write_text("finished")
        publish_directory(staging, output)
        self.assertFalse(staging.exists())
        self.assertEqual((output / "complete").read_text(), "finished")


if __name__ == "__main__":
    unittest.main()
