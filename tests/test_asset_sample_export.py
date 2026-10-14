"""Original-byte fixtures for standalone private sample publication boundaries."""
import copy
from contextlib import ExitStack
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

from host.assets import prepare_assets
from host.rom_prepare import prepare_rom
from tests.romfs_fixtures import ncch_romfs


SCRIPT = Path(__file__).resolve().parents[1] / "tools/export-asset-samples.py"
spec = importlib.util.spec_from_file_location("asset_sample_export", SCRIPT)
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class AssetSampleExportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "Original assets 日本語"
        self.root.mkdir()
        self.manifest = self.root / "asset-manifest.json"
        self.preparation = self.root / "preparation-report.json"
        self.blob = self.root / "romfs.bin"
        self.output = self.root / "samples.zip"
        self.payloads = {"/original/scene.bin": b"original scene bytes\0",
                         "/original/設定.txt": "original 日本語".encode(),
                         "/unselected.bin": b"DO NOT EXPORT THIS ORIGINAL FILE"}
        data, records = bytearray(64), []
        for name, payload in self.payloads.items():
            records.append({"path": name, "offset": len(data), "size": len(payload), "sha256": sha(payload)})
            data.extend(payload)
        self.blob.write_bytes(data)
        identity = {"program_id": "000400000ff00001", "save_identity": "ctr-000400000ff00001",
                    "product_code": "CTR-P-TEST"}
        ivfc = {"level3_size": len(data), "level3_sha256": sha(data),
                "all_declared_blocks_verified": True, "ncch_superblock_anchored": True}
        self.asset = {"ok": True, "stage": "prepare-assets", "schema_version": 1,
                      "identity": identity, "executable_revision": sha(b"original revision"),
                      "source_sha256": sha(b"original prepared main"), "ivfc": ivfc,
                      "romfs_file": {"bytes": len(data), "sha256": sha(data)},
                      "metadata": {"directories": 2, "files": records, "file_data_offset": 64,
                                   "file_bytes": len(data)-64, "guest_paths_only": True,
                                   "host_paths_extracted": False}}
        self.prep = {"ok": True, "stage": "prepare-rom", "schema_version": 1,
                     "identity": copy.deepcopy(identity), "executable_revision": self.asset["executable_revision"],
                     "source_sha256": sha(b"original container"), "output_sha256": self.asset["source_sha256"],
                     "romfs_preserved": True, "keys_exported": False, "other_contents_exported": False,
                     "full_ivfc_verified": True, "romfs_integrity": copy.deepcopy(ivfc)}
        self.write_reports()

    def write_reports(self):
        self.manifest.write_text(json.dumps(self.asset), encoding="utf-8")
        self.preparation.write_text(json.dumps(self.prep), encoding="utf-8")

    def export(self, paths=None, output=None):
        return exporter.export_samples(self.manifest, self.preparation, self.blob,
                                       paths if paths is not None else ["/original/scene.bin"],
                                       output if output is not None else self.output)

    def reject(self, code, paths=None, output=None):
        before = {p: p.read_bytes() for p in (self.manifest, self.preparation, self.blob)}
        with self.assertRaises(exporter.SampleError) as caught:
            self.export(paths, output)
        self.assertEqual(caught.exception.code, code)
        for p, data in before.items():
            self.assertEqual(p.read_bytes(), data)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-samples-*")))

    def test_exact_selected_bytes_unicode_and_bounded_receipt(self):
        self.asset["private_key"] = "must not enter receipt"
        self.asset["identity"]["untrusted_extra"] = "also excluded"
        self.prep["identity"] = copy.deepcopy(self.asset["identity"])
        self.write_reports()
        before = {p: p.read_bytes() for p in (self.manifest, self.preparation, self.blob)}
        paths = ["/original/scene.bin", "/original/設定.txt"]
        result = self.export(paths)
        self.assertEqual(result["files"], 2)
        self.assertLess(result["archive_bytes"], 32*1024**2)
        with zipfile.ZipFile(self.output) as z:
            self.assertEqual(set(z.namelist()), {"files"+p for p in paths} | {"asset-sample-report.json"})
            for p in paths:
                self.assertEqual(z.read("files"+p), self.payloads[p])
            receipt = json.loads(z.read("asset-sample-report.json"))
        self.assertNotIn("untrusted_extra", receipt["identity"])
        self.assertNotIn("private_key", receipt)
        self.assertEqual(receipt["manifest_sha256"], sha(before[self.manifest]))
        self.assertEqual(receipt["preparation_report_sha256"], sha(before[self.preparation]))
        self.assertEqual(receipt["source_blob_sha256"], sha(before[self.blob]))
        self.assertTrue(receipt["selected_file_hashes_verified"])
        for field in ("full_ivfc_reverified", "original_container_hash_verified",
                      "signature_authenticity_verified", "commercial_profile_enabled", "console_verified"):
            self.assertIs(receipt[field], False)
        for p, data in before.items():
            self.assertEqual(p.read_bytes(), data)

    def test_actual_original_preparation_and_asset_reports_are_supported(self):
        original = self.root / "original.cxi"
        original.write_bytes(ncch_romfs())
        prepare_rom(original, self.root / "prepared")
        prepare_assets(self.root / "prepared/game.cxi", self.root / "assets")
        result = exporter.export_samples(self.root / "assets/asset-manifest.json",
                                         self.root / "prepared/preparation-report.json",
                                         self.root / "assets/romfs.bin", ["/levels/設定.txt"], self.output)
        self.assertTrue(result["ok"])
        with zipfile.ZipFile(self.output) as z:
            self.assertEqual(z.read("files/levels/設定.txt"), b"read-only fixture\n")

    def test_cli_runs_isolated_outside_the_project_without_installed_packages(self):
        run = subprocess.run([sys.executable, "-I", str(SCRIPT), "--manifest", str(self.manifest),
                              "--preparation-report", str(self.preparation), "--romfs", str(self.blob),
                              "--paths", "/original/設定.txt", "--output", str(self.output)],
                             cwd=self.root, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr + run.stdout)
        self.assertEqual(json.loads(run.stdout)["files"], 1)
        self.assertNotIn(self.asset["source_sha256"], run.stdout)

    def test_mismatched_prepared_source_revision_and_identity_reject(self):
        original = copy.deepcopy(self.prep)
        for key in ("output_sha256", "executable_revision", "identity"):
            with self.subTest(key=key):
                self.prep = copy.deepcopy(original)
                self.prep[key] = {} if key == "identity" else "f"*64
                self.write_reports()
                self.reject("provenance_mismatch")

    def test_reported_ivfc_must_agree_and_cover_preserved_assets(self):
        original = copy.deepcopy(self.prep)
        for key in ("full_ivfc_verified", "romfs_preserved", "keys_exported", "other_contents_exported", "romfs_integrity"):
            with self.subTest(key=key):
                self.prep = copy.deepcopy(original)
                self.prep[key] = {} if key == "romfs_integrity" else not self.prep[key]
                self.write_reports()
                self.reject("provenance_mismatch")

    def test_corrupted_unselected_bytes_fail_full_blob_hash(self):
        data = bytearray(self.blob.read_bytes())
        data[-1] ^= 1
        self.blob.write_bytes(data)
        self.reject("blob_hash_mismatch")

    def test_wrong_selected_hash_cleans_staging_and_preserves_inputs(self):
        self.asset["metadata"]["files"][0]["sha256"] = "f"*64
        self.write_reports()
        self.reject("file_hash_mismatch")

    def test_blob_size_and_ivfc_hash_bindings_reject(self):
        original = copy.deepcopy(self.asset)
        for change in ("bytes", "sha256"):
            with self.subTest(change=change):
                self.asset = copy.deepcopy(original)
                self.asset["romfs_file"][change] = 1 if change == "bytes" else "f"*64
                self.write_reports()
                self.reject("blob_mismatch")

    def test_unsafe_virtual_names_never_become_archive_members(self):
        paths = ("relative", "/../escape", "/a/./b", "/a//b", "/a\\b", "/a:b", "/a\0b",
                 "/NUL.txt", "/COM1", "/LPT¹.txt", "/trailing.", "/space ", "/"+chr(0xd800), "/"+"a"*513)
        for path in paths:
            with self.subTest(path=repr(path)):
                self.asset["metadata"]["files"][0]["path"] = path
                self.write_reports()
                self.reject("unsafe_guest_path")

    def test_duplicate_case_and_unicode_aliases_are_rejected(self):
        for a, b in (("/same", "/same"), ("/SAME", "/same"), ("/caf\u00e9", "/cafe\u0301")):
            with self.subTest(a=a):
                self.asset["metadata"]["files"][0]["path"] = a
                self.asset["metadata"]["files"][1]["path"] = b
                self.write_reports()
                self.reject("duplicate_guest_path")

    def test_file_directory_alias_is_rejected(self):
        self.asset["metadata"]["files"][0]["path"] = "/original"
        self.write_reports()
        self.reject("duplicate_guest_path")

    def test_invalid_extents_and_overlaps_reject_before_copy(self):
        original = copy.deepcopy(self.asset)
        for offset, size in ((63, 2), (len(self.blob.read_bytes()), 2), (64, len(self.blob.read_bytes())), (65, 21)):
            with self.subTest(offset=offset, size=size):
                self.asset = copy.deepcopy(original)
                self.asset["metadata"]["files"][0].update(offset=offset, size=size)
                self.asset["metadata"]["file_bytes"] = sum(r["size"] for r in self.asset["metadata"]["files"])
                self.write_reports()
                self.reject("invalid_extent")

    def test_boolean_negative_and_bad_hash_fields_reject(self):
        original = copy.deepcopy(self.asset)
        for key, value in (("size", True), ("offset", -1), ("sha256", "F"*64), ("sha256", "not a digest")):
            with self.subTest(key=key, value=value):
                self.asset = copy.deepcopy(original)
                self.asset["metadata"]["files"][0][key] = value
                self.write_reports()
                self.reject("invalid_manifest")

    def test_inconsistent_inventory_byte_total_rejects(self):
        self.asset["metadata"]["file_bytes"] += 1
        self.write_reports()
        self.reject("invalid_manifest")

    def test_explicit_selection_limits_and_exact_names(self):
        self.reject("selection_limit", [])
        self.reject("selection_limit", ["/original/scene.bin"]*65)
        self.reject("duplicate_selection", ["/original/scene.bin"]*2)
        self.reject("missing_guest_path", ["/ORIGINAL/scene.bin"])
        with patch.object(exporter, "MAX_SELECTED_BYTES", 1):
            self.reject("selection_limit")

    def test_completed_zip_limit_is_enforced_before_publication(self):
        with patch.object(exporter, "MAX_ZIP_BYTES", 1):
            self.reject("archive_size_limit")

    def test_report_size_duplicate_keys_nonfinite_and_invalid_json_reject(self):
        original = self.manifest.read_bytes()
        with patch.object(exporter, "MAX_REPORT_BYTES", 1):
            self.reject("report_size_limit")
        for data in (b'{"ok":true,"ok":true}', b'{"ok":NaN}', b'\xff', b'{'):
            self.manifest.write_bytes(data)
            self.reject("invalid_report")
        self.manifest.write_bytes(original)

    def test_existing_output_and_source_aliases_are_preserved(self):
        self.output.write_bytes(b"keep existing output")
        with self.assertRaises(exporter.SampleError) as caught:
            self.export()
        self.assertEqual(caught.exception.code, "output_exists")
        self.assertEqual(self.output.read_bytes(), b"keep existing output")
        self.output.unlink()
        self.reject("output_exists", output=self.blob)

    def test_symbolic_inputs_and_dangling_output_are_preserved(self):
        link = self.root / "input-link.bin"
        try:
            link.symlink_to(self.blob)
        except OSError:
            self.skipTest("Creating symlinks requires an unavailable platform privilege")
        with self.assertRaises(exporter.SampleError) as caught:
            exporter.export_samples(self.manifest, self.preparation, link, ["/original/scene.bin"], self.output)
        self.assertEqual(caught.exception.code, "unsafe_input")
        self.output.symlink_to(self.root / "missing-output-target")
        with self.assertRaises(exporter.SampleError) as caught:
            self.export()
        self.assertEqual(caught.exception.code, "output_exists")
        self.assertTrue(self.output.is_symlink())

    def test_publication_race_never_replaces_new_output(self):
        publish = exporter.publish
        def race(stage, output):
            output.write_bytes(b"keep raced output")
            publish(stage, output)
        with patch.object(exporter, "publish", race), self.assertRaises(exporter.SampleError) as caught:
            self.export()
        self.assertEqual(caught.exception.code, "output_exists")
        self.assertEqual(self.output.read_bytes(), b"keep raced output")
        self.assertFalse(list(self.root.glob(".ctr2wup-samples-*")))

    def test_blob_change_during_zip_blocks_publication(self):
        chunks = exporter.Input.chunks
        def change(source, offset, size):
            yield from chunks(source, offset, size)
            if source.path == self.blob and offset == 64:
                with self.blob.open("r+b") as f:
                    f.seek(0)
                    f.write(b"changed!")
        with patch.object(exporter.Input, "chunks", change), self.assertRaises(exporter.SampleError) as caught:
            self.export()
        self.assertEqual(caught.exception.code, "input_changed")
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-samples-*")))

    def test_changed_report_during_zip_blocks_publication(self):
        entry = exporter.zip_entry
        def change(name):
            self.preparation.write_bytes(self.preparation.read_bytes()+b" ")
            return entry(name)
        with patch.object(exporter, "zip_entry", change), self.assertRaises(exporter.SampleError) as caught:
            self.export()
        self.assertEqual(caught.exception.code, "input_changed")
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".ctr2wup-samples-*")))

    def test_path_and_handle_timestamp_precision_need_not_match(self):
        actual = self.blob.stat()
        handle = SimpleNamespace(st_mode=actual.st_mode, st_dev=actual.st_dev,
                                 st_ino=actual.st_ino, st_size=actual.st_size,
                                 st_mtime_ns=actual.st_mtime_ns//10**9*10**9,
                                 st_ctime_ns=actual.st_ctime_ns//10**9*10**9)
        # Model Windows CRT fstat precision while path stat retains nanoseconds.
        with ExitStack() as stack, patch.object(exporter.os, "fstat", return_value=handle):
            source = exporter.Input(self.blob, stack)
            source.unchanged()
            self.assertEqual(source.sha256(), sha(self.blob.read_bytes()))
            self.blob.write_bytes(self.blob.read_bytes()+b"changed")
            with self.assertRaises(exporter.SampleError) as caught:
                source.unchanged()
            self.assertEqual(caught.exception.code, "input_changed")


if __name__ == "__main__":
    unittest.main()
