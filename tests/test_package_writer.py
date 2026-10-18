"""Original offline inputs only. These certificates cannot authenticate a title."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from unittest.mock import patch

from host.errors import HostError
from host.package_crypto import PackagingInputs, certificate_subjects, cipher
from host.package_integrity import verify_package
from host.package_validator import CONTENT_START, validate_package
from host.package_writer import pack_title


def synthetic_certificate(issuer, name):
    """Invented test names, zero signature, arbitrary original public-key bytes."""
    data = bytearray(0x300)
    struct.pack_into(">I", data, 0, 0x10004)
    data[0x140:0x140 + len(issuer)] = issuer.encode("ascii")
    struct.pack_into(">I", data, 0x180, 1)
    data[0x184:0x184 + len(name)] = name.encode("ascii")
    data[0x1C8:0x2C8] = bytes([0x81]) * 256
    struct.pack_into(">I", data, 0x2C8, 65537)
    return bytes(data)


def original_inputs(root):
    source = root / "original staging 日本語"
    for folder in ("code", "content/empty", "content/sub", "meta"):
        (source / folder).mkdir(parents=True, exist_ok=True)
    app = ('<app><title_id>00050000cf520001</title_id><title_version>0007</title_version>'
           '<group_id>1337</group_id><os_version>000500101000400a</os_version>'
           '<app_type>80000000</app_type></app>')
    (source / "code/app.xml").write_text(app, encoding="utf-8")
    (source / "code/cos.xml").write_text('<app><argstr>probe.rpx</argstr></app>')
    (source / "meta/meta.xml").write_text('<menu><title_id>00050000cf520001</title_id><title_version>7</title_version></menu>')
    # Original structural fixture with WUT FILEINFO/CRC; no executable code.
    rpx = bytearray(280)
    rpx[:7] = b"\x7fELF\x01\x02\x01"
    rpx[7] = 0xCA
    struct.pack_into(">HHI", rpx, 16, 0xFE01, 20, 1)
    struct.pack_into(">I", rpx, 32, 52)
    struct.pack_into(">H", rpx, 40, 52)
    struct.pack_into(">HH", rpx, 46, 40, 3)
    struct.pack_into('>10I',rpx,92,0,0x80000004,0,0,172,96,0,0,4,0)
    struct.pack_into('>10I',rpx,132,0,0x80000003,0,0,268,12,0,0,4,4)
    struct.pack_into('>I',rpx,172,0xcafe0402)
    struct.pack_into('>I',rpx,172+0x34,2)
    struct.pack_into('>I',rpx,172+0x40,0x5078)
    struct.pack_into('>I',rpx,172+0x50,0x5335)
    struct.pack_into('>I',rpx,272,zlib.crc32(rpx[172:268]))
    (source / "code/probe.rpx").write_bytes(rpx)
    (source / "content/sub/bytes.bin").write_bytes(bytes(range(256)) * 9000 + b"unaligned")
    (source / "content/zero.bin").write_bytes(b"")
    (source / "content/sub/日本語.bin").write_bytes(b"Original UTF-8 path test")
    (root / "synthetic-common.key").write_bytes(bytes(range(16)))
    (root / "synthetic.cert").write_bytes(synthetic_certificate("Root", "CACTR2TEST")
            + synthetic_certificate("Root-CACTR2TEST", "CPCTR2TEST")
            + synthetic_certificate("Root-CACTR2TEST", "XSCTR2TEST"))
    config = root / "local-config.json"
    config.write_text(json.dumps({"schema_version": 1, "signature_policy": "homebrew_unsigned",
                                 "common_key_file": "synthetic-common.key", "certificate_file": "synthetic.cert"}))
    return source, config, root / "synthetic-common.key"


def rehash_record(package, index, plain):
    """Repair test metadata after deliberate plaintext corruption, independently."""
    path = package / "title.tmd"
    data = bytearray(path.read_bytes())
    start = CONTENT_START + index * 48
    data[start + 16:start + 48] = hashlib.sha1(plain).digest() + bytes(12)
    data[0x208:0x228] = hashlib.sha256(data[CONTENT_START:]).digest()
    data[0x1E4:0x204] = hashlib.sha256(data[0x204:CONTENT_START]).digest()
    path.write_bytes(data)


@unittest.skipUnless(importlib.util.find_spec("cryptography"), "install ctr2wup[packaging] for offline encryption tests")
class WriterTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.source, self.config, self.key_file = original_inputs(self.root)
        self.output = self.root / "encrypted output 日本語"

    def tearDown(self):
        self.tmp.cleanup()

    def packed(self):
        return pack_title(self.source, self.output, self.config)

    def rejected(self, action, code):
        with self.assertRaises(HostError) as caught:
            action()
        self.assertEqual(caught.exception.code, code)

    def title_key(self):
        ticket = (self.output / "title.tik").read_bytes()
        engine = cipher(self.key_file.read_bytes(), ticket[0x1DC:0x1E4] + bytes(8)).decryptor()
        return engine.update(ticket[0x1BF:0x1CF]) + engine.finalize()

    def corrupt_plaintext(self, index, mutate):
        key = self.title_key()
        iv = index.to_bytes(2, "big") + bytes(14)
        path = self.output / f"{index:08x}.app"
        reader = cipher(key, iv).decryptor()
        data = bytearray(reader.update(path.read_bytes()) + reader.finalize())
        mutate(data)
        writer = cipher(key, iv).encryptor()
        path.write_bytes(writer.update(data) + writer.finalize())
        rehash_record(self.output, index, data)

    def test_roundtrip_files_empty_directories_and_no_install_claim(self):
        result = self.packed()
        self.assertEqual(result["validation"], "decrypted_integrity")
        self.assertEqual(result["staging_comparison"], "all files and directories match")
        self.assertEqual(len(result["files"]), 7)
        self.assertEqual(result["title_version"], 7)
        self.assertFalse(result["signatures"]["authenticated"])
        self.assertFalse(result["console_verified"])
        self.assertEqual(result["installation"], "unverified")
        self.assertEqual(validate_package(self.output)["content_count"], 4)
        self.assertEqual({p.name for p in self.output.iterdir()}, {
            "00000000.app", "00000001.app", "00000002.app", "00000003.app",
            "title.tmd", "title.tik", "title.cert", "pack-report.json"})
        text = json.dumps(result)
        self.assertNotIn(str(self.key_file), text)
        self.assertNotIn(self.key_file.read_bytes().hex(), text)
        self.assertEqual(repr(PackagingInputs(self.config)), "PackagingInputs(<local inputs redacted>)")

    @unittest.skipUnless(shutil.which("openssl"), "independent OpenSSL executable unavailable")
    def test_openssl_decrypts_ticket_and_every_content_independently(self):
        self.packed()
        # Only original synthetic test keys are used in these subprocesses.
        ticket = (self.output / "title.tik").read_bytes()
        key = subprocess.run(["openssl", "enc", "-aes-128-cbc", "-d", "-nopad", "-K",
                              bytes(range(16)).hex(), "-iv", "00050000cf520001" + "00" * 8],
                             input=ticket[0x1BF:0x1CF], capture_output=True, check=True).stdout
        self.assertEqual(len(key), 16)
        tmd = (self.output / "title.tmd").read_bytes()
        for index in range(4):
            plain = subprocess.run(["openssl", "enc", "-aes-128-cbc", "-d", "-nopad", "-K", key.hex(),
                                    "-iv", index.to_bytes(2, "big").hex() + "00" * 14],
                                   input=(self.output / f"{index:08x}.app").read_bytes(),
                                   capture_output=True, check=True).stdout
            offset = CONTENT_START + index * 48
            self.assertEqual(hashlib.sha1(plain).digest(), tmd[offset + 16:offset + 36])
            self.assertEqual(len(plain), struct.unpack_from(">Q", tmd, offset + 8)[0])
            if index == 0:
                self.assertEqual(plain[:12], b"FST\0" + struct.pack(">II", 32, 4))
            if index == 2:
                # Empty file precedes the subdirectory in the tree, while the
                # independently decoded content preserves actual file bytes.
                self.assertIn((self.source / "content/sub/bytes.bin").read_bytes(), plain)

    def test_wrong_common_key(self):
        self.packed()
        self.key_file.write_bytes(bytes([0xEE]) * 16)
        self.rejected(lambda: verify_package(self.output, self.key_file), "plaintext_digest_mismatch")

    def test_corrupted_ciphertext(self):
        self.packed()
        path = self.output / "00000000.app"
        data = bytearray(path.read_bytes()); data[40] ^= 1; path.write_bytes(data)
        self.rejected(lambda: verify_package(self.output, self.key_file), "plaintext_digest_mismatch")

    def test_fst_path_traversal_even_with_repaired_hashes(self):
        self.packed()
        def mutate(data):
            offset = data.index(b"sub\0")
            data[offset:offset + 3] = b"../"
        self.corrupt_plaintext(0, mutate)
        self.rejected(lambda: verify_package(self.output, self.key_file), "invalid_fst")

    def test_fst_bad_parent_even_with_repaired_hashes(self):
        self.packed()
        self.corrupt_plaintext(0, lambda data: struct.pack_into(">I", data, 0xA0 + 16 + 4, 99))
        self.rejected(lambda: verify_package(self.output, self.key_file), "invalid_fst")

    def test_fst_file_out_of_bounds_even_with_repaired_hashes(self):
        self.packed()
        # Node 2 is code/app.xml. Its offset is a 32-byte-unit integer.
        self.corrupt_plaintext(0, lambda data: struct.pack_into(">I", data, 0xA0 + 2 * 16 + 4, 0xFFFFFFFF))
        self.rejected(lambda: verify_package(self.output, self.key_file), "invalid_fst")

    def test_case_colliding_staging_names_rejected(self):
        (self.source / "content/CASE.bin").write_bytes(b"one")
        (self.source / "content/case.bin").write_bytes(b"two")
        if len([p for p in (self.source / "content").iterdir() if p.name.casefold() == "case.bin"]) == 1:
            self.skipTest("case-insensitive filesystem prevents creating this input")
        self.rejected(self.packed, "unsafe_staging")

    def test_source_change_during_encryption_aborts_publication(self):
        from host.package_writer import _encrypt_group
        def change(*args, **kwargs):
            if args[5] == 2:
                (self.source / "content/sub/bytes.bin").write_bytes(b"changed during build")
            return _encrypt_group(*args, **kwargs)
        with patch("host.package_writer._encrypt_group", side_effect=change):
            self.rejected(self.packed, "input_changed")
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-package-*")))

    def test_nonzero_content_padding_even_with_repaired_hashes(self):
        self.packed()
        self.corrupt_plaintext(3, lambda data: data.__setitem__(-1, 42))
        self.rejected(lambda: verify_package(self.output, self.key_file), "invalid_content_padding")

    def test_expected_staging_detects_changed_assets(self):
        self.packed()
        (self.source / "content/sub/bytes.bin").write_bytes(b"changed")
        self.rejected(lambda: verify_package(self.output, self.key_file, expected_staging=self.source), "staging_mismatch")

    def test_ticket_rights_missing(self):
        self.packed()
        path = self.output / "title.tik"
        data = bytearray(path.read_bytes()); data[0x2D0] = 1; path.write_bytes(data)
        self.rejected(lambda: verify_package(self.output, self.key_file), "invalid_ticket_rights")

    def test_xml_identity_mismatch(self):
        path = self.source / "meta/meta.xml"
        path.write_text(path.read_text().replace("00050000cf520001", "00050000cf520002"))
        self.rejected(self.packed, "invalid_title_metadata")
        self.assertFalse(self.output.exists())

    def test_explicit_unsigned_policy_required(self):
        doc = json.loads(self.config.read_text()); doc["signature_policy"] = "retail_signed"
        self.config.write_text(json.dumps(doc))
        self.rejected(self.packed, "invalid_packaging_config")
        self.assertFalse(self.output.exists())

    def test_credentials_cannot_be_packed_as_content(self):
        key = self.source / "content/copied.key"
        key.write_bytes(bytes(range(16)))
        doc = json.loads(self.config.read_text()); doc["common_key_file"] = str(key)
        self.config.write_text(json.dumps(doc))
        self.rejected(self.packed, "unsafe_staging")

    def test_bad_key_does_not_echo_content_or_path(self):
        self.key_file.write_bytes(b"redacted-private-input")
        with self.assertRaises(HostError) as caught:
            self.packed()
        text = json.dumps(caught.exception.as_dict())
        self.assertNotIn("redacted-private-input", text)
        self.assertNotIn(str(self.key_file), text)

    def test_existing_destination_is_preserved(self):
        self.output.mkdir(); (self.output / "keep").write_bytes(b"existing")
        self.rejected(self.packed, "output_exists")
        self.assertEqual((self.output / "keep").read_bytes(), b"existing")

    def test_cancellation_publishes_nothing(self):
        with patch("host.package_writer._encrypt_group", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                self.packed()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-package-*")))

    @unittest.skipIf(__import__("os").name == "nt", "symlink privileges vary on Windows")
    def test_symlink_input_rejected(self):
        (self.source / "content/link").symlink_to(self.key_file)
        self.rejected(self.packed, "unsafe_staging")

    def test_certificate_missing_parent_or_truncation(self):
        with self.assertRaises(HostError):
            certificate_subjects(synthetic_certificate("Root-MISSING", "CPCTR2TEST"))
        with self.assertRaises(HostError):
            certificate_subjects((self.root / "synthetic.cert").read_bytes()[:-1])


if __name__ == "__main__":
    unittest.main()
