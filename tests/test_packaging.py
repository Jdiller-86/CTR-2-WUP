import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from host.package_validator import (CONTENT_START, ValidatorError,
                                    reserve_title_id, validate_package)


def structural_fixture(root, *, hashed=False):
    """Original fake encrypted bytes for parser tests; NEVER an installable title."""
    root.mkdir(exist_ok=True)
    title = bytes.fromhex("0005000043540001")
    tmd = bytearray(CONTENT_START + 0x30)
    struct.pack_into(">I", tmd, 0, 0x10004)
    tmd[0x180] = 1
    tmd[0x18C:0x194] = title
    struct.pack_into(">H", tmd, 0x1DE, 1)
    size = 0x10000 if hashed else 16
    struct.pack_into(">IHHQ", tmd, CONTENT_START, 1, 0, 0x2003 if hashed else 0x2001, size)
    if hashed:
        h3 = bytes(range(20))
        (root / "00000001.h3").write_bytes(h3)
        tmd[CONTENT_START + 16:CONTENT_START + 36] = hashlib.sha1(h3).digest()
    else:
        tmd[CONTENT_START + 16:CONTENT_START + 48] = hashlib.sha256(b"original parser test").digest()
    repair_tmd(tmd)
    (root / "title.tmd").write_bytes(tmd)
    ticket = bytearray(0x350)
    struct.pack_into(">I", ticket, 0, 0x10004)
    ticket[0x1DC:0x1E4] = title
    (root / "title.tik").write_bytes(ticket)
    (root / "title.cert").write_bytes(b"TEST certificate not authenticated")
    (root / "00000001.app").write_bytes(bytes(size))
    return root


def repair_tmd(tmd):
    struct.pack_into(">HH", tmd, 0x204, 0, 1)
    tmd[0x208:0x228] = hashlib.sha256(tmd[CONTENT_START:]).digest()
    tmd[0x1E4:0x204] = hashlib.sha256(tmd[0x204:CONTENT_START]).digest()


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.pkg = structural_fixture(self.root / "test package 日本語")

    def tearDown(self):
        self.tmp.cleanup()

    def rejected(self, code):
        with self.assertRaises(ValidatorError) as ctx:
            validate_package(self.pkg)
        self.assertEqual(ctx.exception.code, code)

    def test_structural_pass_does_not_claim_authenticity(self):
        result = validate_package(self.pkg)
        self.assertTrue(result["ok"])
        self.assertEqual(result["validation"], "structure_only")
        self.assertFalse(result["signatures"]["authenticated"])
        self.assertEqual(result["contents"][0]["plaintext_integrity"], "unverified_requires_local_key")
        self.assertEqual(result["installation"], "unverified")

    def test_hashed_content_metadata(self):
        pkg = structural_fixture(self.root / "hashed", hashed=True)
        self.assertEqual(validate_package(pkg)["contents"][0]["h3_integrity"], "sha1_matches_tmd")
        (pkg / "00000001.h3").write_bytes(b"x" * 20)
        with self.assertRaises(ValidatorError) as ctx:
            validate_package(pkg)
        self.assertEqual(ctx.exception.code, "content_digest_mismatch")

    def test_truncation(self):
        (self.pkg / "title.tmd").write_bytes(b"x")
        self.rejected("truncated_tmd")

    def test_table_integrity(self):
        path = self.pkg / "title.tmd"
        tmd = bytearray(path.read_bytes())
        tmd[0x205] ^= 1
        path.write_bytes(tmd)
        self.rejected("metadata_digest_mismatch")

    def test_content_metadata_integrity(self):
        path = self.pkg / "title.tmd"
        tmd = bytearray(path.read_bytes())
        tmd[CONTENT_START + 20] ^= 1
        path.write_bytes(tmd)
        self.rejected("metadata_digest_mismatch")

    def test_system_id_rejected(self):
        path = self.pkg / "title.tmd"
        tmd = bytearray(path.read_bytes())
        tmd[0x18C:0x194] = bytes.fromhex("000500101000400a")
        path.write_bytes(tmd)
        self.rejected("invalid_title_id")

    def test_ticket_title_mismatch(self):
        path = self.pkg / "title.tik"
        ticket = bytearray(path.read_bytes())
        ticket[0x1E3] ^= 1
        path.write_bytes(ticket)
        self.rejected("ticket_title_mismatch")

    def test_content_size(self):
        (self.pkg / "00000001.app").write_bytes(b"x")
        self.rejected("content_size_mismatch")

    def test_missing_content(self):
        (self.pkg / "00000001.app").unlink()
        self.rejected("missing_content")

    def test_unexpected_content(self):
        (self.pkg / "00000002.app").write_bytes(bytes(16))
        self.rejected("unexpected_content")

    def test_unsupported_content(self):
        path = self.pkg / "title.tmd"
        tmd = bytearray(path.read_bytes())
        struct.pack_into(">H", tmd, CONTENT_START + 6, 0x2000)
        repair_tmd(tmd)
        path.write_bytes(tmd)
        self.rejected("unsupported_content_type")

    def test_registry_stable_and_collision(self):
        registry = self.root / "registry.json"
        report = validate_package(self.pkg)
        reserve_title_id(registry, report, "original parser fixture")
        reserve_title_id(registry, report, "original parser fixture")
        self.assertEqual(validate_package(self.pkg, registry)["title_id_registry"]["status"], "matches_registered_package")
        with self.assertRaises(ValidatorError) as ctx:
            reserve_title_id(registry, report, "another project")
        self.assertEqual(ctx.exception.code, "title_id_collision")
        document = json.loads(registry.read_text())
        document["titles"][report["title_id"]]["tmd_sha256"] = "0" * 64
        registry.write_text(json.dumps(document))
        with self.assertRaises(ValidatorError) as ctx:
            validate_package(self.pkg, registry)
        self.assertEqual(ctx.exception.code, "title_id_collision")

    @unittest.skipIf(__import__("os").name == "nt", "symlink capability depends on Windows privileges")
    def test_symlink_rejected(self):
        app = self.pkg / "00000001.app"
        app.unlink()
        app.symlink_to(self.root / "elsewhere")
        self.rejected("unsafe_file")


if __name__ == "__main__":
    unittest.main()
