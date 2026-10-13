"""Original cartridge/data fixtures for the reported auxiliary-CFA intake failure."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from host.containers import inspect
from host.errors import HostError
from host.intake_export import export_intake
from host.rom_prepare import prepare_rom
from tests.host_fixtures import ncch, ncsd, PROGRAM_ID


def original_cfa():
    payload = (b"original-CFA-only-asset" * 24)[:512].ljust(512, b"\0")
    data = bytearray(512) + payload
    data[0x100:0x104] = b"NCCH"
    struct.pack_into("<I", data, 0x104, 2)
    struct.pack_into("<Q", data, 0x108, PROGRAM_ID + 1)
    struct.pack_into("<Q", data, 0x118, PROGRAM_ID)
    data[0x18C] = 1
    data[0x18D] = 1  # Data, not executable. CFA format version is zero.
    data[0x18F] = 4  # Decrypted, with a RomFS section.
    struct.pack_into("<III", data, 0x1B0, 1, 1, 1)
    data[0x1E0:0x200] = hashlib.sha256(payload).digest()
    return data


class CfaIntakeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.source = self.root / "Original cartridge 日本語.cci"

    def tearDown(self):
        self.tmp.cleanup()

    def reject(self, cfa, code):
        data = ncsd([(0, ncch()), (1, cfa)])
        self.source.write_bytes(data)
        with self.assertRaises(HostError) as caught:
            export_intake(self.source, self.root / "rejected")
        self.assertEqual(caught.exception.code, code)
        self.assertEqual(self.source.read_bytes(), data)
        self.assertFalse((self.root / "rejected").exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-export-*")))

    def test_decrypted_cartridge_data_partition_preserves_executable_revision(self):
        main = ncch()
        data = ncsd([(0, main), (1, original_cfa())])
        self.source.write_bytes(data)
        standalone = self.root / "main.cxi"
        standalone.write_bytes(main)
        checked = inspect(self.source)
        self.assertEqual(checked["executable_revision"], inspect(standalone)["executable_revision"])
        self.assertEqual([p["ncch_version"] for p in checked["partitions"]], [2, 0])
        self.assertFalse(checked["partitions"][1]["executable"])
        report = export_intake(self.source, self.root / "intake")
        self.assertTrue(report["executable_available"])
        self.assertFalse(report["romfs_exported"])
        self.assertEqual(inspect(self.root / "intake/executable-view.cxi")["executable_revision"], checked["executable_revision"])
        with zipfile.ZipFile(report["upload_archive"]) as archive:
            self.assertEqual(set(archive.namelist()), {"intake-report.json", "executable-view.cxi"})
            self.assertNotIn(b"original-CFA-only-asset", archive.read("executable-view.cxi"))
        self.assertEqual(self.source.read_bytes(), data)

    def test_supported_main_preparation_is_independent_of_auxiliary_format(self):
        cfa = original_cfa()
        struct.pack_into("<H", cfa, 0x112, 1)  # Still unsupported by inspection.
        data = ncsd([(0, ncch()), (1, cfa)])
        self.source.write_bytes(data)
        prepare_rom(self.source, self.root / "prepared")
        report = export_intake(self.root / "prepared/game.cxi", self.root / "intake")
        self.assertTrue(report["executable_available"])
        self.assertEqual(self.source.read_bytes(), data)

    def test_prototype_version_remains_rejected(self):
        cfa = original_cfa()
        struct.pack_into("<H", cfa, 0x112, 1)
        self.reject(cfa, "unsupported_ncch_version")

    def test_v0_executable_claim_remains_rejected(self):
        cfa = original_cfa()
        cfa[0x18D] = 3
        self.reject(cfa, "unsupported_ncch_version")

    def test_v0_extended_header_remains_rejected(self):
        cfa = original_cfa()
        struct.pack_into("<I", cfa, 0x180, 0x400)
        self.reject(cfa, "unsupported_ncch_version")

    def test_encrypted_auxiliary_is_reported_without_executable_export(self):
        cfa = original_cfa()
        cfa[0x18F] &= ~4
        data = ncsd([(0, ncch()), (1, cfa)])
        self.source.write_bytes(data)
        with self.assertRaises(HostError) as caught:
            inspect(self.source)
        self.assertEqual(caught.exception.code, "encrypted_input")
        report = export_intake(self.source, self.root / "intake")
        self.assertFalse(report["executable_available"])
        with zipfile.ZipFile(report["upload_archive"]) as archive:
            self.assertEqual(archive.namelist(), ["intake-report.json"])
            self.assertEqual(json.loads(archive.read("intake-report.json"))["blocking_error"]["code"], "encrypted_input")
        self.assertEqual(self.source.read_bytes(), data)

    def test_cfa_hash_mismatch_cannot_publish_intake(self):
        cfa = original_cfa()
        cfa[-1] ^= 1
        self.reject(cfa, "hash_mismatch")

    def test_cfa_out_of_bounds_section_cannot_publish_intake(self):
        cfa = original_cfa()
        struct.pack_into("<I", cfa, 0x1B0, 10)
        self.reject(cfa, "invalid_bounds")


if __name__ == "__main__":
    unittest.main()
