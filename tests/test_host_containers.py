import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

from host.containers import decompress_code, inspect
from host.errors import HostError
from tests.host_fixtures import CODE, PROGRAM_ID, blz_repeat, ncch, ncsd, rehash_exefs, smdh


class ContainerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "a path 日本語.cxi"
        self.addCleanup(self.tmp.cleanup)

    def parse(self, data):
        self.path.write_bytes(data)
        return inspect(self.path)

    def rejects(self, data, code):
        with self.assertRaises(HostError) as caught:
            self.parse(data)
        self.assertEqual(caught.exception.code, code)

    def test_ncch_little_endian_and_revision_identity(self):
        result = self.parse(ncch())
        self.assertEqual(result["format"], "NCCH")
        self.assertEqual(result["identity"]["program_id"], f"{PROGRAM_ID:016x}")
        self.assertEqual(result["partitions"][0]["segments"]["text"]["address"], 0x100000)
        self.assertEqual(result["partitions"][0]["executable_sha256"], hashlib.sha256(CODE).hexdigest())
        changed = self.parse(ncch([(".code", CODE[:-1] + b"\0")]))
        self.assertEqual(result["identity"], changed["identity"])
        self.assertNotEqual(result["executable_revision"], changed["executable_revision"])
        self.assertFalse(result["integrity"]["signature_authenticity_verified"])

    def test_ncch_compressed_code_hash(self):
        result = self.parse(ncch([(".code", blz_repeat())], compressed=True))
        self.assertEqual(result["partitions"][0]["executable_sha256"], hashlib.sha256(b"A" * 36).hexdigest())
        self.assertEqual(result["partitions"][0]["executable_size"], 36)
        self.assertTrue(result["partitions"][0]["code_compressed"])

    def test_ncsd_partition_selection_and_trimmed_capacity(self):
        data = ncsd()
        struct.pack_into("<I", data, 0x104, len(data) // 0x200 + 128)
        result = self.parse(data)
        self.assertEqual(result["format"], "NCSD")
        self.assertEqual(result["partitions"][0]["offset"], 0x200)
        self.assertGreater(result["media_capacity"], result["file_size"])

    def test_ncsd_checks_every_partition_encryption(self):
        second = ncch()
        second[0x18F] &= ~4
        self.rejects(ncsd([(0, ncch()), (1, second)]), "encrypted_input")

    def test_smdh_title(self):
        result = self.parse(ncch([(".code", CODE), ("icon", smdh())]))
        title = result["partitions"][0]["title"]
        self.assertEqual(title["short_title"], "CTR2WUP synthetic")
        self.assertEqual(title["region_mask"], 2)

    def test_truncated_headers(self):
        for size in (0, 4, 0x100, 0x1FF):
            with self.subTest(size=size):
                self.rejects(bytes(size), "truncated_header")

    def test_unsupported_magic_and_3dsx(self):
        self.rejects(bytes(0x200), "unsupported_format")
        self.path = self.path.with_suffix(".3dsx")
        self.rejects(ncch(), "unsupported_format")

    def test_ncch_encryption(self):
        data = ncch()
        data[0x18F] = 0
        self.rejects(data, "encrypted_input")

    def test_ncch_version_and_unit_limit(self):
        data = ncch()
        struct.pack_into("<H", data, 0x112, 0)
        self.rejects(data, "unsupported_ncch_version")
        data = ncch()
        data[0x18E] = 32
        self.rejects(data, "invalid_unit_size")

    def test_ncch_exceeds_available(self):
        data = ncch()
        struct.pack_into("<I", data, 0x104, 0xFFFFFFFF)
        self.rejects(data, "invalid_bounds")

    def test_exheader_size_and_hash(self):
        data = ncch()
        struct.pack_into("<I", data, 0x180, 0xFFFFFFFF)
        self.rejects(data, "unsupported_exheader")
        data = ncch()
        data[0x210] ^= 1
        self.rejects(data, "hash_mismatch")

    def test_exefs_outside_ncch(self):
        data = ncch()
        struct.pack_into("<I", data, 0x1A0, 0xFFFFFFFF)
        self.rejects(data, "invalid_bounds")

    def test_exefs_size_without_offset(self):
        data = ncch()
        struct.pack_into("<I", data, 0x1A0, 0)
        self.rejects(data, "invalid_bounds")

    def test_overlapping_ncch_sections(self):
        data = ncch()
        struct.pack_into("<II", data, 0x190, 5, 1)
        self.rejects(data, "overlapping_regions")

    def test_hash_region_must_exist_within_section(self):
        for size in (0, 0xFFFFFFFF):
            data = ncch()
            struct.pack_into("<I", data, 0x1A8, size)
            self.rejects(data, "invalid_hash_region")

    def test_exefs_superblock_hash(self):
        data = ncch()
        data[0xA10] ^= 1
        self.rejects(data, "hash_mismatch")

    def test_exefs_payload_hash(self):
        data = ncch()
        data[0xC00] ^= 1
        self.rejects(data, "hash_mismatch")

    def test_exefs_payload_bounds(self):
        data = ncch()
        struct.pack_into("<I", data, 0xA08, 0xFFFFFFFF)
        rehash_exefs(data)
        self.rejects(data, "invalid_bounds")
        data = ncch()
        struct.pack_into("<I", data, 0xA0C, 0xFFFFFFFF)
        rehash_exefs(data)
        self.rejects(data, "invalid_bounds")

    def test_exefs_names_reject_traversal_and_windows_paths(self):
        for name in ("../x", "a/b", "a\\b", "..", "x:y", "a?b"):
            with self.subTest(name=name):
                self.rejects(ncch([(name, CODE)]), "invalid_exefs_name")

    def test_exefs_name_padding(self):
        data = ncch()
        data[0xA00:0xA08] = b".code\0X\0"
        rehash_exefs(data)
        self.rejects(data, "invalid_exefs_name")

    def test_duplicate_casefold_names(self):
        self.rejects(ncch([(".code", CODE), (".CODE", CODE)]), "duplicate_exefs_name")

    def test_overlapping_exefs(self):
        data = ncch([(".code", CODE), ("banner", CODE)])
        struct.pack_into("<I", data, 0xA18, 0)
        rehash_exefs(data)
        self.rejects(data, "overlapping_regions")

    def test_exefs_unnamed_and_zero_entry(self):
        data = ncch()
        struct.pack_into("<I", data, 0xA18, 1)
        rehash_exefs(data)
        self.rejects(data, "invalid_exefs_entry")
        self.rejects(ncch([(".code", b"")]), "invalid_exefs_entry")

    def test_missing_code_and_invalid_smdh(self):
        self.rejects(ncch([("banner", CODE)]), "missing_code")
        self.rejects(ncch([(".code", CODE), ("icon", b"SMDH")]), "invalid_metadata")

    def test_all_ten_exefs_slots(self):
        files = [(".code", CODE)] + [(f"f{index}", bytes([index])) for index in range(1, 10)]
        result = self.parse(ncch(files))
        self.assertEqual(len(result["partitions"][0]["exefs"]), 10)

    def test_ncsd_empty_and_missing_primary(self):
        data = bytearray(0x200)
        data[0x100:0x104] = b"NCSD"
        struct.pack_into("<I", data, 0x104, 1)
        self.rejects(data, "missing_main_partition")
        self.rejects(ncsd([(1, ncch())]), "missing_main_partition")

    def test_ncsd_partition_bounds_and_overlap(self):
        data = ncsd()
        struct.pack_into("<I", data, 0x120, 0xFFFFFFFF)
        self.rejects(data, "invalid_bounds")
        data = ncsd([(0, ncch()), (1, ncch())])
        struct.pack_into("<I", data, 0x128, 1)
        self.rejects(data, "overlapping_regions")

    def test_ncsd_capacity_and_nand_type(self):
        data = ncsd()
        struct.pack_into("<I", data, 0x104, 1)
        self.rejects(data, "invalid_bounds")
        data = ncsd()
        data[0x110] = 1
        self.rejects(data, "unsupported_partition")

    def test_standalone_trailing_data(self):
        self.rejects(ncch() + bytes(0x200), "trailing_data")

    def test_input_missing(self):
        with self.assertRaises(HostError) as caught:
            inspect(self.path)
        self.assertEqual(caught.exception.code, "input_io")


class DecompressionTests(unittest.TestCase):
    def rejects(self, data, code="invalid_compression", **kwargs):
        with self.assertRaises(HostError) as caught:
            decompress_code(data, **kwargs)
        self.assertEqual(caught.exception.code, code)

    def test_overlapping_run(self):
        self.assertEqual(decompress_code(blz_repeat()), b"A" * 36)

    def test_uncompressed_prefix_preserved(self):
        self.assertEqual(decompress_code(b"prefix!" + blz_repeat()), b"prefix!" + b"A" * 36)

    def test_footer_truncation(self):
        for size in range(8):
            self.rejects(bytes(size))

    def test_invalid_footer_bounds(self):
        for footer, span in ((7, 16), (20, 16), (8, 17)):
            data = bytearray(blz_repeat())
            struct.pack_into("<I", data, 8, footer << 24 | span)
            self.rejects(data)

    def test_growth_limit_before_allocation(self):
        data = bytearray(blz_repeat())
        struct.pack_into("<I", data, 12, 0xFFFFFFFF)
        self.rejects(data, "decompression_limit")
        self.rejects(blz_repeat(), "decompression_limit", max_output=35)

    def test_unproduced_reference(self):
        data = bytearray(blz_repeat())
        data[2] = 1
        self.rejects(data)

    def test_declared_size_mismatch(self):
        for growth in (0, 19, 21):
            data = bytearray(blz_repeat())
            struct.pack_into("<I", data, 12, growth)
            self.rejects(data)

    def test_truncated_token(self):
        self.rejects(bytes.fromhex("008009000008") + struct.pack("<I", 0))


if __name__ == "__main__":
    unittest.main()
