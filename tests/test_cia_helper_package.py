import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("cia_helper_package", ROOT / "tools/package-cia-helper.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class CiaHelperPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.executable = bytearray(128)
        self.executable[:2] = b"MZ"
        struct.pack_into("<I", self.executable, 60, 64)
        self.executable[64:68] = b"PE\0\0"
        struct.pack_into("<H", self.executable, 68, 0x8664)
        files = {
            "artifacts/cia-helper/windows/CTR2WUP-CIA-Intake.exe": bytes(self.executable),
            "tools/CiaIntake/Program.cs": b"Original helper source fixture",
            "LICENSE": b"Original project license fixture",
            "docs/licenses/dotnet-runtime-LICENSE.txt": b"Original runtime license fixture",
            "docs/licenses/dotnet-runtime-THIRD-PARTY-NOTICES.txt": b"Original runtime notice fixture",
            "docs/cia-intake.md": b"Original helper usage fixture",
        }
        for name, data in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.report = {
            "ok": True, "version": PACKAGE.__version__,
            "helper_sha256": hashlib.sha256(self.executable).hexdigest(),
            "source_sha256": hashlib.sha256(files["tools/CiaIntake/Program.cs"]).hexdigest(),
            "platform": "win32 .NET fixture execution", "native_windows_execution": "verified",
            "checks": [{"name": "original-fixture", "ok": True}],
        }

    def package(self):
        path = self.root / "artifacts/reports/cia-helper-final/report.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.report))
        with patch.object(PACKAGE, "ROOT", self.root), patch("builtins.print"):
            return PACKAGE.main()

    def test_exact_native_receipt_is_retained_in_the_distributed_manifest(self):
        self.assertEqual(self.package(), 0)
        archives = list((self.root / "artifacts/delivery").glob("*.zip"))
        self.assertEqual(len(archives), 1)
        with zipfile.ZipFile(archives[0]) as archive:
            self.assertIsNone(archive.testzip())
            report = json.loads(archive.read("build-report.json"))
            self.assertEqual(archive.read("CTR2WUP-CIA-Intake.exe"), bytes(self.executable))
        self.assertEqual(report["native_windows_execution"], "verified")
        self.assertEqual(report["fixture_platform"], "win32 .NET fixture execution")
        self.assertEqual(report["fixture_checks"], 1)
        self.assertEqual(report["linux_dotnet_fixture_checks"], 0)

    def test_other_executable_source_or_platform_cannot_promote_native_evidence(self):
        for key, value in (("helper_sha256", "0" * 64), ("source_sha256", "0" * 64),
                           ("platform", "linux .NET fixture execution")):
            with self.subTest(key=key):
                original = self.report[key]
                self.report[key] = value
                with self.assertRaisesRegex(RuntimeError, "exact executable and current source"):
                    self.package()
                self.assertFalse((self.root / "artifacts/delivery").exists())
                self.report[key] = original

    def test_missing_or_failed_case_receipts_never_publish(self):
        for checks in ([], [{"name": "original-fixture", "ok": False}]):
            with self.subTest(checks=checks):
                self.report["checks"] = checks
                with self.assertRaisesRegex(RuntimeError, "successful fixture checks"):
                    self.package()
                self.assertFalse((self.root / "artifacts/delivery").exists())

    def test_linux_receipt_stays_unverified_for_native_windows(self):
        self.report["native_windows_execution"] = "unverified"
        self.report["platform"] = "Linux .NET execution; Windows target cross-published separately"
        self.assertEqual(self.package(), 0)
        path = next((self.root / "artifacts/delivery").glob("cia-helper-manifest-*.json"))
        report = json.loads(path.read_text())
        self.assertEqual(report["native_windows_execution"], "unverified")
        self.assertEqual(report["linux_dotnet_fixture_checks"], 1)


if __name__ == "__main__":
    unittest.main()
