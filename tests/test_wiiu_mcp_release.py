"""Original wire, installer and release-integrity fixtures; no physical console."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import struct
import tempfile
import threading
import tomllib
import unittest
from unittest.mock import patch
import zipfile
import zlib

from host.wiiload import send

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


setup = module("mcp_kit_setup", "tools/templates/wiiu-mcp-setup.py")
packager = module("mcp_kit_packager", "tools/package-wiiu-mcp.py")


class WiiloadTests(unittest.TestCase):
    def test_actual_tcp_frame_and_exact_decompressed_payload(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(3)
        received = bytearray()
        errors = []

        def receive():
            try:
                peer, _ = listener.accept()
                with peer:
                    peer.settimeout(3)
                    while block := peer.recv(4096):
                        received.extend(block)
            except OSError as error:
                errors.append(type(error).__name__)

        thread = threading.Thread(target=receive, daemon=True)
        thread.start()
        payload = b"WUHB" + bytes(range(256)) * 20
        try:
            result = send("127.0.0.1", "original.wuhb", payload, port=listener.getsockname()[1])
            thread.join(4)
        finally:
            listener.close()
        self.assertFalse(errors)
        self.assertEqual(received[:6], b"HAXX\x00\x05")
        arg_size, wire_size, plain_size = struct.unpack(">HII", received[6:16])
        self.assertEqual(plain_size, len(payload))
        self.assertEqual(zlib.decompress(received[16:16 + wire_size]), payload)
        self.assertEqual(received[16 + wire_size:], b"original.wuhb\0\0")
        self.assertEqual(arg_size, len(b"original.wuhb\0\0"))
        self.assertEqual(result["wire_bytes"], len(received))
        self.assertFalse(result["execution_confirmed"])

    def test_send_error_never_reconnects(self):
        with patch("host.wiiload.socket.create_connection") as connect:
            connect.return_value.__enter__.return_value.sendall.side_effect = TimeoutError("fixture")
            with self.assertRaises(TimeoutError):
                send("127.0.0.1", "original.wuhb", b"WUHBoriginal")
            self.assertEqual(connect.call_count, 1)

    def test_bad_wire_arguments_never_connect(self):
        with patch("host.wiiload.socket.create_connection") as connect:
            for filename in ("../app.wuhb", "app\n.wuhb", "app\0.wuhb", "app\\a.wuhb"):
                with self.subTest(filename=filename), self.assertRaises(ValueError):
                    send("127.0.0.1", filename, b"WUHBfixture")
            connect.assert_not_called()


class SetupTests(unittest.TestCase):
    def fixture(self, root):
        content = io.BytesIO()
        with zipfile.ZipFile(content, "w") as archive:
            archive.writestr("CTR2WUP/", "")
            archive.writestr("CTR2WUP/runtime/wiiu/Makefile", "# original fixture\n")
        files = {"source.zip": content.getvalue(), "sd-card/wiiu/apps/ctr2wup-prototype.wuhb": b"WUHBfixture"}
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        manifest = {"kind": "ctr2wup-wiiu-mcp-kit", "source_commit": "a" * 40,
                    "files": {n: {"bytes": len(d), "sha256": hashlib.sha256(d).hexdigest()} for n, d in files.items()}}
        (root / "manifest.json").write_text(json.dumps(manifest))

    def test_offline_prepare_valid_config_and_repeat_without_overwrites(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            report = setup.prepare(root, "192.0.2.1", install=False)
            self.assertFalse(report["console_contacted"])
            config = tomllib.loads(Path(report["bridge_configuration"]).read_text())
            self.assertEqual(config["console_host"], "192.0.2.1")
            self.assertEqual(config["wiiload"], "builtin")
            self.assertTrue((Path(report["project"]) / report["starter_payload"]).is_file())
            self.assertIn("wiiu", tomllib.loads(Path(report["mcp_configuration"]).read_text())["mcp_servers"])
            setup.prepare(root, "192.0.2.1", install=False)
            with self.assertRaises(ValueError):
                setup.prepare(root, "192.0.2.2", install=False)
            self.assertEqual(tomllib.loads(Path(report["bridge_configuration"]).read_text())["console_host"], "192.0.2.1")

    def test_modified_kit_is_rejected_before_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "source.zip").write_bytes(b"changed")
            with self.assertRaises(ValueError):
                setup.prepare(root, "192.0.2.1", install=False)
            self.assertFalse((root / "local").exists())

    def test_source_traversal_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with zipfile.ZipFile(root / "source.zip", "w") as archive:
                archive.writestr("CTR2WUP/../../outside.txt", "fixture")
            with self.assertRaises(ValueError):
                setup.extract_source(root / "source.zip", root / "destination")
            self.assertFalse((root / "destination").exists())


class PackagingTests(unittest.TestCase):
    def test_cached_download_hash_and_https_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "component.zip"
            target.write_bytes(b"original")
            with patch("urllib.request.urlopen") as network:
                self.assertEqual(packager.fetch_verified("https://example.test/file", target, 8,
                    hashlib.sha256(b"original").hexdigest()), b"original")
                with self.assertRaises(ValueError):
                    packager.fetch_verified("https://example.test/file", target, 8, "0" * 64)
                with self.assertRaises(ValueError):
                    packager.fetch_verified("http://example.test/file", target, 8, "0" * 64)
                network.assert_not_called()

    def test_console_dependency_closure_and_pins(self):
        lock = json.loads((ROOT / "packaging/wiiu-mcp-components.json").read_text())
        self.assertEqual(len(lock["components"]), 10)
        targets = [row["sd_path"] for row in lock["components"]]
        self.assertEqual(len(set(targets)), 10)
        self.assertTrue(any(name.endswith("setup/10_wums_loader.rpx") for name in targets))
        for row in lock["components"]:
            self.assertRegex(row["sha256"], "^[0-9a-f]{64}$")
            self.assertRegex(row["source_sha256"], "^[0-9a-f]{64}$")
            self.assertIn(row["commit"], row["source_url"])


if __name__ == "__main__":
    unittest.main()
