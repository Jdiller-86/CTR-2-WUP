"""Original loopback fixtures: no console, credentials or commercial data."""
import asyncio
from concurrent.futures import ThreadPoolExecutor
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import io
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from host.wiiu_bridge import BridgeConfig, MAX_FILE, SD_ROOT, UDPLogs, WiiUBridge, local_path, relative_path
from host.wiiu_mcp import main

try:
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import FTPHandler
    from pyftpdlib.servers import FTPServer
except ImportError:
    FTPServer = None


class BridgeUnitTests(unittest.TestCase):
    def test_literal_paths(self):
        for value in ("/ctr2wup/a", "ctr2wup/../a", "ctr2wup//a", "a/./b", "a\\b", "a\r\nDELE b", "a\x00", "a/", "a/b.", "a/b ", "C:/a", "a/NUL", "a/com1.txt"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                relative_path(value)
        self.assertEqual(relative_path("ctr2wup/results/original test.txt"), "ctr2wup/results/original test.txt")
        self.assertEqual(relative_path("ctr2wup/results/original-é.txt"), "ctr2wup/results/original-é.txt")

    def test_symlink_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            try:
                (root / "link").symlink_to(root / "outside", target_is_directory=True)
            except OSError:
                self.skipTest("Symlink creation unavailable")
            with self.assertRaises(ValueError):
                local_path(root, "link/file")

    def test_listing_is_bounded_and_rejects_duplicates_and_traversal(self):
        class Listing:
            def __init__(self, rows):
                self.rows = rows

            def retrlines(self, command, receive):
                for row in self.rows:
                    receive(row)

        for rows in (["type=file;size=2; ../outside"], ["type=file;size=x; test"],
                     ["type=file;size=1; a", "type=file;size=1; a"],
                     ["type=file;size=1; " + str(i) for i in range(1025)]):
            with self.subTest(rows=rows[:2]), self.assertRaises(ValueError):
                WiiUBridge._listing(Listing(rows), SD_ROOT)

    def test_streaming_size_guards(self):
        class Download:
            def retrbinary(self, command, receive):
                receive(b"123")
        with self.assertRaises(ValueError):
            WiiUBridge._read(Download(), "/a", 2)
        with self.assertRaises(ValueError):
            WiiUBridge._read(Download(), "/a", 4)
        with self.assertRaises(ValueError):
            WiiUBridge._read(Download(), "/a", MAX_FILE + 1)

    def test_udp_capture_cursor_filter_and_cleanup(self):
        receiver = UDPLogs("127.0.0.1")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        try:
            capture = receiver.start(port)
            self.assertEqual(capture["bind_address"], "127.0.0.1")
            self.assertEqual(receiver.socket.getsockname(), ("127.0.0.1", port))
            with self.assertRaises(ValueError):
                receiver.start(port)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                sender.sendto(b"original console fixture", ("127.0.0.1", port))
            deadline = time.monotonic() + 2
            while not receiver.read(0)["datagrams"] and time.monotonic() < deadline:
                time.sleep(0.01)
            result = receiver.read(0)
            self.assertEqual(result["datagrams"][0]["text"], "original console fixture")
            self.assertEqual(receiver.read(result["next_cursor"])["datagrams"], [])
            with receiver.lock:
                for sequence in range(2, 140):
                    receiver.rows.append({"sequence": sequence})
                receiver.sequence = 139
            self.assertTrue(receiver.read(0)["buffer_overrun"])
            self.assertEqual(len(receiver.read(0)["datagrams"]), 128)
        finally:
            receiver.close()
        self.assertFalse(receiver.read(0)["listening"])
        # A different expected source rejects local packets without retaining them.
        receiver = UDPLogs("192.0.2.1", bind_address="127.0.0.1")
        try:
            receiver.start(port)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                sender.sendto(b"wrong source", ("127.0.0.1", port))
            time.sleep(0.05)
            self.assertEqual(receiver.read(0)["datagrams"], [])
        finally:
            receiver.close()


@unittest.skipUnless(FTPServer, "Install .[mcp-test] for real FTP/MCP loopback integration")
class BridgeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.base = Path(cls.temporary.name)
        cls.sd = cls.base / "ftp" / SD_ROOT.lstrip("/")
        cls.sd.mkdir(parents=True)
        cls.project = cls.base / "project"
        (cls.project / "runtime/wiiu").mkdir(parents=True)
        (cls.project / "runtime/wiiu/Makefile").write_text("# original fixture\n")
        authorizer = DummyAuthorizer()
        authorizer.add_user("fixture", "local-only", str(cls.base / "ftp"), perm="elradfmwMT")

        class Handler(FTPHandler):
            def log(self, *args, **kwargs):
                pass

            def logline(self, *args, **kwargs):
                pass

        Handler.authorizer = authorizer
        cls.server = FTPServer(("127.0.0.1", 0), Handler)
        cls.port = cls.server.socket.getsockname()[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever,
                                      kwargs={"timeout": 0.05, "handle_exit": False}, daemon=True)
        cls.thread.start()
        cls.environment = patch.dict("os.environ", {"CTR2WUP_FTP_USER": "fixture", "CTR2WUP_FTP_PASSWORD": "local-only"})
        cls.environment.start()

    @classmethod
    def tearDownClass(cls):
        cls.environment.stop()
        cls.server.close_all()
        cls.thread.join(2)
        cls.temporary.cleanup()

    def setUp(self):
        self.bridge = WiiUBridge(BridgeConfig("127.0.0.1", self.project, ftp_port=self.port, timeout=2))
        self.addCleanup(self.bridge.close)

    def config_file(self):
        path = self.base / "bridge.toml"
        path.write_text('console_host = "127.0.0.1"\nproject_root = ' + json.dumps(str(self.project))
                        + '\nftp_port = ' + str(self.port) + '\ntimeout = 2\n')
        return path

    def test_real_ftp_upload_read_download_and_preserve_previous_files(self):
        data = b"original fixture\n" + bytes(range(256))
        (self.project / "fixture.bin").write_bytes(data)
        receipt = self.bridge.upload("fixture.bin", "ctr2wup/transfer/fixture.bin")
        self.assertTrue(receipt["readback_verified"])
        self.assertEqual((self.sd / "ctr2wup/transfer/fixture.bin").read_bytes(), data)
        self.assertEqual(self.bridge.read_text("ctr2wup/transfer/fixture.bin")["bytes"], len(data))
        self.assertEqual(self.bridge.list_files("ctr2wup/transfer")["entries"][0]["type"], "file")
        result = self.bridge.download("ctr2wup/transfer/fixture.bin", "fixture-copy.bin")
        self.assertEqual(Path(result["local_file"]).read_bytes(), data)
        with self.assertRaises(ValueError):
            self.bridge.upload("fixture.bin", "ctr2wup/transfer/fixture.bin")
        with self.assertRaises(ValueError):
            self.bridge.download("ctr2wup/transfer/fixture.bin", "fixture-copy.bin")
        self.assertEqual(list((self.sd / "ctr2wup/transfer").glob("*.part")), [])

    def test_restricted_paths_and_unknown_capabilities(self):
        for path in ("wiiu/environments/aroma/plugins/a.wps", "sys/logs/a", "ctr2wup/../../a"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.bridge.upload("fixture.bin", path)
        capabilities = self.bridge.capabilities()
        self.assertFalse(capabilities["hardware_verified"])
        self.assertIn("controller_injection", capabilities["unavailable"])

    def test_collect_nested_results_hashes_and_private_location(self):
        root = self.sd / "ctr2wup/collect-fixture"
        (root / "nested").mkdir(parents=True)
        (root / "trace.txt").write_text("incomplete original trace\n")
        (root / "nested/result.txt").write_text("original result fixture\n")
        result = self.bridge.collect("ctr2wup/collect-fixture")
        self.assertTrue(result["transport_complete"])
        self.assertEqual(result["test_verdict"], "not_evaluated")
        self.assertEqual(len(result["files"]), 2)
        self.assertTrue(Path(result["output"]).is_relative_to(self.project / "private/wiiu"))
        self.assertEqual(json.loads((Path(result["output"]) / "collection.json").read_text())["files"], result["files"])
        next_result = self.bridge.collect("ctr2wup/collect-fixture")
        self.assertNotEqual(next_result["output"], result["output"])

    def test_failed_collection_retains_incomplete_receipt(self):
        root = self.sd / "ctr2wup/oversize-fixture"
        root.mkdir(parents=True)
        with (root / "large.bin").open("wb") as stream:
            stream.truncate(MAX_FILE + 1)
        before = set((self.project / "private/wiiu").glob("collection-*"))
        with self.assertRaises(ValueError):
            self.bridge.collect("ctr2wup/oversize-fixture")
        created = set((self.project / "private/wiiu").glob("collection-*")) - before
        self.assertEqual(len(created), 1)
        receipt = json.loads((created.pop() / "collection.json").read_text())
        self.assertFalse(receipt["transport_complete"])

    def test_corrupt_upload_is_not_published(self):
        (self.project / "corruption.bin").write_bytes(b"original")
        with patch.object(self.bridge, "_read", return_value=b"modified"), self.assertRaises(ValueError):
            self.bridge.upload("corruption.bin", "ctr2wup/corrupt/fixture.bin")
        self.assertFalse((self.sd / "ctr2wup/corrupt/fixture.bin").exists())
        self.assertEqual(list((self.sd / "ctr2wup/corrupt").iterdir()), [])

    def test_launch_uses_argument_vector_and_no_automatic_retry(self):
        self.bridge = WiiUBridge(BridgeConfig("127.0.0.1", self.project, wiiload="wiiload"))
        (self.project / "original fixture.wuhb").write_bytes(b"WUHBoriginal fixture")
        with patch.object(self.bridge, "_wiiload", return_value="official-wiiload"), patch("host.wiiu_bridge.subprocess.run") as run:
            run.return_value = subprocess.CompletedProcess([], 0)
            result = self.bridge.launch(str(self.project / "original fixture.wuhb"))
            self.assertTrue(result["submitted"])
            self.assertFalse(result["execution_confirmed"])
            command = run.call_args.args[0]
            self.assertEqual(command[0], "official-wiiload")
            self.assertEqual(Path(command[1]).name, "original fixture.wuhb")
            self.assertTrue(Path(command[1]).is_relative_to(self.project / "private/wiiu"))
            self.assertFalse(Path(command[1]).exists())
            self.assertEqual(run.call_args.kwargs["env"]["WIILOAD"], "tcp:127.0.0.1")
            self.assertNotIn("shell", run.call_args.kwargs)
            run.reset_mock()
            run.side_effect = subprocess.TimeoutExpired("wiiload", 60)
            with self.assertRaises(subprocess.TimeoutExpired):
                self.bridge.launch("original fixture.wuhb")
            self.assertEqual(run.call_count, 1)

    def test_paired_install_stages_a_verified_wup_and_signs_the_request(self):
        try:
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        except ImportError:
            self.skipTest("Install .[packaging] for the signed console control fixture")
        from host.package_homebrew import pack_homebrew
        from host.title_stage import stage_title
        from host.wiiu_control import ENTRY, HEADER, INSTALL, ROOT
        from tests.test_package_writer import original_inputs

        bridge = WiiUBridge(replace(self.bridge.config, control_key="private/control.key"))
        self.addCleanup(bridge.close)
        pairing = bridge.pair_control()
        key = Ed25519PublicKey.from_public_bytes(Path(pairing["public_file"]).read_bytes())

        mailbox = self.sd / ROOT
        mailbox.mkdir(parents=True)
        session, sequence = "0000000000000718", "00000001"
        (mailbox / "state.txt").write_bytes(
            f"CTR1 {session} {sequence} 0005001010040100 {pairing['pairing_id']} 1 -\n".encode())
        title = "0005000043543257"
        source, _, _ = original_inputs(self.project / "control-fixture")
        stage = self.project / "control-stage"
        package = self.project / "control-wup"
        stage_title(source / "code/probe.rpx", stage, title_id=title, version=7)
        pack_homebrew(stage, package, require_startup=True)

        with ThreadPoolExecutor(max_workers=1) as executor:
            pending = executor.submit(bridge.install, str(package), title, "usb")
            request = mailbox / f"req-{session}-{sequence}.bin"
            deadline = time.monotonic() + 8
            while not request.exists() and time.monotonic() < deadline:
                if pending.done():
                    self.fail(f"Expected the install request, received {pending.result()}")
                time.sleep(0.02)
            packet = request.read_bytes()
            key.verify(packet[:64], packet[64:])
            magic, epoch, index, action, target, count, installed, version, title_id, job = HEADER.unpack_from(packet, 64)
            self.assertEqual((magic, epoch, index, action, target, version, title_id),
                             (b"CTR1", int(session, 16), 1, INSTALL, 1, 7, int(title, 16)))
            self.assertGreater(installed, 0)
            self.assertEqual(len(packet), 64 + HEADER.size + (count + installed) * ENTRY.size)
            staged = self.sd / "install" / ("ctr2wup-" + job.hex())
            self.assertEqual(sorted(file.name for file in staged.iterdir()),
                             sorted(file.name for file in package.iterdir() if file.name != "pack-report.json"))
            self.assertEqual((staged / "title.tmd").read_bytes(), (package / "title.tmd").read_bytes())
            (mailbox / f"result-{job.hex()}.txt").write_bytes(
                f"CTR1 {session} {sequence} {job.hex()} installed_verified 0\n".encode())
            result = pending.result(timeout=5)
        self.assertEqual((result["state"], result["operation_id"]), ("installed_verified", job.hex()))

    def test_cli_no_network_capabilities_and_clean_errors(self):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            code = main(["--config", str(self.config_file()), "--action", "capabilities"])
        self.assertEqual(code, 0)
        self.assertEqual(errors.getvalue(), "")
        self.assertEqual(json.loads(output.getvalue())["mcp_transport"], "stdio")

    def test_real_mcp_stdio_discovery_ftp_call_and_tool_error(self):
        try:
            from mcp import ClientSession, StdioServerParameters
            from mcp.client.stdio import stdio_client
        except ImportError:
            self.skipTest("Install .[mcp-test] for MCP integration")

        async def check():
            from os import environ
            parameters = StdioServerParameters(command=sys.executable,
                args=["-m", "host.wiiu_mcp", "--config", str(self.config_file())], env=dict(environ))
            async with stdio_client(parameters) as (read, write):
                async with ClientSession(read, write, read_timeout_seconds=10) as session:
                    await session.initialize()
                    tools = await session.list_tools()
                    self.assertEqual(len(tools.tools), 12)
                    self.assertIn("wiiu_launch", {tool.name for tool in tools.tools})
                    capabilities = await session.call_tool("wiiu_capabilities", {})
                    self.assertFalse(capabilities.is_error)
                    status = await session.call_tool("wiiu_status", {})
                    self.assertFalse(status.is_error)
                    self.assertTrue(status.structured_content["sd_readable"])
                    rejected = await session.call_tool("wiiu_list_files", {"sd_path": "../outside"})
                    self.assertTrue(rejected.is_error)
        asyncio.run(check())


if __name__ == "__main__":
    unittest.main()
