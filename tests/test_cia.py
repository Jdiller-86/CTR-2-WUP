import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from host.containers import Reader, inspect
from host.cia import read_cia
from host.errors import HostError
from host.intake_export import export_intake
from host.loader import load_image
from host.guest_fixture import fixture_bytes
from tests.host_fixtures import CODE, PROGRAM_ID, cia, ncch


class CiaTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.path = self.root / "Original CIA 日本語.cia"
        self.path.write_bytes(cia())

    def tearDown(self):self.tmp.cleanup()

    def reject(self, data, code):
        self.path.write_bytes(data)
        with self.assertRaises(HostError) as caught:inspect(self.path)
        self.assertEqual(caught.exception.code, code)

    def test_real_cia_offset_revision_and_version(self):
        result = inspect(self.path)
        cxi = self.root / "base.cxi"; cxi.write_bytes(ncch())
        self.assertEqual(result["executable_revision"], inspect(cxi)["executable_revision"])
        self.assertEqual(result["format"], "CIA")
        self.assertEqual(result["cia"]["title_version"], 48)
        self.assertGreater(result["partitions"][0]["offset"], 0x2020)
        self.assertFalse(result["integrity"]["signature_authenticity_verified"])

    def test_checked_loader_reads_cia_guest_offsets(self):
        original = fixture_bytes()
        image = self.root / "framebuffer.cxi"; image.write_bytes(original)
        title = inspect(image)["identity"]["program_id"]
        self.path.write_bytes(cia([(0, original)], title_id=int(title, 16)))
        from_cia, from_cxi = load_image(self.path), load_image(image)
        self.assertEqual(from_cia["code"], from_cxi["code"])
        self.assertEqual(from_cia["entry"], from_cxi["entry"])
        self.assertEqual(from_cia["segments"], from_cxi["segments"])

    def test_sparse_bitmap_maps_index_8_without_creating_index_1(self):
        self.path.write_bytes(cia([(0, bytes(ncch())), (8, b"original non-executable auxiliary content")]))
        result = inspect(self.path)
        self.assertEqual([x["index"] for x in result["cia"]["contents"]], [0, 8])

    def test_encrypted_outer_payload_metadata_still_exports(self):
        self.path.write_bytes(cia(encrypted=True))
        with self.assertRaises(HostError) as caught:inspect(self.path)
        self.assertEqual(caught.exception.code, "encrypted_cia_content")
        report = export_intake(self.path, self.root / "intake")
        self.assertFalse(report["executable_available"])
        self.assertFalse(report["credentials_exported"])
        with zipfile.ZipFile(report["upload_archive"]) as z:
            self.assertEqual(z.namelist(), ["intake-report.json"])
            self.assertEqual(json.loads(z.read("intake-report.json"))["cia"]["title_id"], f"{PROGRAM_ID:016x}")

    def test_inner_ncch_encryption_visible(self):
        image = ncch(); image[0x18F] &= ~4
        self.path.write_bytes(cia([(0, image)]))
        result = export_intake(self.path, self.root / "intake")
        self.assertEqual(result["blocking_error"]["code"], "encrypted_input")

    def test_bad_tmd_group_hash(self):
        data = cia()
        with self.path.open("rb") as file:meta = read_cia(Reader(file))
        data[meta["sections"]["tmd"]["offset"] + 0xB04 + 16] ^= 1
        self.reject(data, "cia_metadata_hash_mismatch")

    def test_bad_content_hash(self):
        data = cia(); data[-1] ^= 1
        self.reject(data, "cia_content_hash_mismatch")

    def test_bad_content_bitmap(self):
        data = cia(); data[0x20] |= 0x40
        self.reject(data, "invalid_cia_bitmap")

    def test_declared_blob_size_outside_file(self):
        data = cia(); struct.pack_into("<Q", data, 0x18, 2**63)
        self.reject(data, "invalid_bounds")

    def test_header_type(self):
        data = cia(); struct.pack_into("<H", data, 4, 1)
        self.reject(data, "unsupported_cia_header")

    def test_trailing_data(self):self.reject(cia() + b"junk", "trailing_cia_data")

    def test_ticket_title_mismatch(self):
        data = cia()
        with self.path.open("rb") as file:meta = read_cia(Reader(file))
        data[meta["sections"]["ticket"]["offset"] + 0x1DC] ^= 1
        self.reject(data, "cia_ticket_title_mismatch")

    def test_main_absent(self):self.reject(cia([(8, bytes(ncch()))]), "missing_cia_executable")

    def test_update_rejected(self):
        self.reject(cia(title_id=0x0004000E00054000), "unsupported_cia_title")

    def test_small_view_drops_romfs_and_preserves_executable(self):
        image = ncch(); offset = len(image); payload = b"Original large RomFS placeholder".ljust(0x20000, b"\0")
        image += payload
        image[0x18F] &= ~2
        struct.pack_into("<I", image, 0x104, len(image)//512)
        struct.pack_into("<III", image, 0x1B0, offset//512, len(payload)//512, 1)
        image[0x1E0:0x200] = hashlib.sha256(payload[:512]).digest()
        self.path.write_bytes(cia([(0, image)]))
        original = self.path.read_bytes()
        report = export_intake(self.path, self.root / "intake 日本語")
        self.assertTrue(report["executable_available"])
        view = self.root / "intake 日本語/executable-view.cxi"
        self.assertEqual(report["executable_revision"], inspect(view)["executable_revision"])
        self.assertNotIn("romfs", inspect(view)["partitions"][0]["sections"])
        self.assertNotIn(b"Original large RomFS", view.read_bytes())
        self.assertEqual(self.path.read_bytes(), original)
        with zipfile.ZipFile(report["upload_archive"]) as z:
            self.assertEqual(set(z.namelist()), {"intake-report.json", "executable-view.cxi"})

    def test_existing_destination_preserved(self):
        output = self.root / "intake"; output.mkdir(); (output / "keep").write_text("keep")
        with self.assertRaises(HostError):export_intake(self.path, output)
        self.assertEqual((output / "keep").read_text(), "keep")


if __name__ == "__main__":unittest.main()
