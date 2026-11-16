"""Original installer fixtures. All installations, cards and Codex settings are temporary."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import tomllib
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


backend = load("wizard_setup", "tools/templates/wiiu-mcp-setup.py")
actions = load("wizard_actions", "tools/templates/wiiu-mcp-installer-actions.py")
windows_package = load("wizard_windows_package", "tools/package-wiiu-mcp-windows.py")


def fixture(root, commit="a" * 40):
    source = io.BytesIO()
    with zipfile.ZipFile(source, "w") as archive:
        archive.writestr("CTR2WUP/", "")
        archive.writestr("CTR2WUP/runtime/wiiu/Makefile", "# original fixture\n")
    files = {
        "source.zip": source.getvalue(),
        "runtime/WiiU-MCP-Bridge.exe": b"MZ original fixture, not executable",
        "sd-card/wiiu/apps/ctr2wup-prototype.wuhb": b"WUHBoriginal",
        "sd-card/wiiu/environments/aroma/plugins/ftpiiu.wps": b"new original plugin",
        "sd-card/wiiu/environments/aroma/modules/Original.wms": b"new original module",
    }
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    manifest = {"kind": "ctr2wup-wiiu-mcp-kit", "source_commit": commit, "version": "0.1.0-alpha.2",
                "files": {n: {"bytes": len(d), "sha256": hashlib.sha256(d).hexdigest()} for n, d in files.items()}}
    (root / "manifest.json").write_text(json.dumps(manifest))
    (root / "SHA256SUMS.txt").write_text("original fixture\n")


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.kit = self.root / "kit"
        self.kit.mkdir()
        fixture(self.kit)

    def test_frozen_install_needs_no_python_install_and_no_console_address(self):
        target = self.root / "My installed bridge"
        with patch.object(backend.venv, "create") as create, patch.object(backend.subprocess, "run") as process:
            result = actions.install(backend, self.kit, target)
        create.assert_not_called()
        process.assert_not_called()
        self.assertTrue(result["prepared"])
        self.assertFalse(result["console_contacted"])
        self.assertEqual(result["command"], [str(target / "runtime/WiiU-MCP-Bridge.exe")])
        self.assertFalse((target / "local/wiiu.toml").exists())
        self.assertTrue(json.loads((target / "local/install-state.json").read_text())["complete"])

    def test_existing_unrelated_directory_is_preserved(self):
        target = self.root / "unrelated"
        target.mkdir()
        (target / "important.txt").write_text("keep")
        with self.assertRaises(ValueError):
            actions.install(backend, self.kit, target)
        self.assertEqual(list(target.iterdir()), [target / "important.txt"])

    def test_update_preserves_private_files_and_uses_new_source_snapshot(self):
        target = self.root / "install"
        first = actions.install(backend, self.kit, target)
        (target / "local/notes.txt").write_text("keep original evidence")
        fixture(self.kit, commit="b" * 40)
        second = actions.install(backend, self.kit, target)
        self.assertNotEqual(first["project"], second["project"])
        self.assertTrue(Path(first["project"]).is_dir())
        self.assertEqual((target / "local/notes.txt").read_text(), "keep original evidence")

    def test_connection_update_preserves_custom_roots_and_backs_up_previous_settings(self):
        target = self.root / "install"
        actions.install(backend, self.kit, target)
        first = actions.configure(backend, target, "192.0.2.1")
        config = Path(first["configuration"])
        with config.open("a") as stream:
            stream.write('read_roots = ["ctr2wup/results"]\n')
        actions.configure(backend, target, "192.0.2.2")
        values = tomllib.loads(config.read_text())
        self.assertEqual(values["console_host"], "192.0.2.2")
        self.assertEqual(values["read_roots"], ["ctr2wup/results"])
        self.assertTrue(list(config.parent.glob("wiiu.toml.ctr2wup-backup-*")))

    def card(self):
        card = self.root / "card"
        (card / "wiiu/environments/aroma/plugins").mkdir(parents=True)
        (card / "wiiu/environments/aroma/modules").mkdir()
        (card / "wiiu/environments/aroma/plugins/ftpiiu.wps").write_bytes(b"old original plugin")
        (card / "wiiu/environments/aroma/modules/Original.wms").write_bytes(b"old original module")
        (card / "personal-save.bin").write_bytes(b"original save fixture")
        return card

    def test_sd_plan_backups_and_exact_readback_preserve_unrelated_save(self):
        card = self.card()
        plan = actions.sd_plan(backend, self.kit, card)
        self.assertEqual((plan["new"], plan["replace"], plan["same"]), (1, 2, 0))
        result = actions.copy_sd(backend, self.kit, plan)
        self.assertEqual(result["copied"], 3)
        backup = Path(result["backup"])
        self.assertEqual((backup / "wiiu/environments/aroma/plugins/ftpiiu.wps").read_bytes(), b"old original plugin")
        self.assertEqual((card / "wiiu/environments/aroma/plugins/ftpiiu.wps").read_bytes(), b"new original plugin")
        self.assertEqual((card / "personal-save.bin").read_bytes(), b"original save fixture")
        repeated = actions.copy_sd(backend, self.kit, actions.sd_plan(backend, self.kit, card))
        self.assertEqual(repeated, {"copied": 0, "backup": None})

    def test_changed_card_is_rejected_before_any_copy(self):
        card = self.card()
        plan = actions.sd_plan(backend, self.kit, card)
        (card / "wiiu/environments/aroma/plugins/ftpiiu.wps").write_bytes(b"new external file")
        with self.assertRaises(ValueError):
            actions.copy_sd(backend, self.kit, plan)
        self.assertFalse((card / "wiiu/apps").exists())
        self.assertFalse((card / "ctr2wup/installer-backups").exists())

    def test_failed_backup_does_not_publish_any_new_sd_files(self):
        card = self.card()
        plan = actions.sd_plan(backend, self.kit, card)
        with patch.object(actions, "atomic_write", side_effect=OSError("original disk fixture")), self.assertRaises(OSError):
            actions.copy_sd(backend, self.kit, plan)
        self.assertEqual((card / "wiiu/environments/aroma/plugins/ftpiiu.wps").read_bytes(), b"old original plugin")
        self.assertFalse((card / "wiiu/apps").exists())

    def test_wrong_sd_root_is_rejected(self):
        with self.assertRaises(ValueError):
            actions.sd_plan(backend, self.kit, self.root)

    def test_codex_registration_preserves_unrelated_tables_and_can_update_owned_entry(self):
        target = self.root / "codex/config.toml"
        target.parent.mkdir()
        original = 'model = "original-fixture"\n# keep this comment\n[mcp_servers.other]\ncommand = "original"\n'
        target.write_text(original)
        client = backend.client_configuration(["C:/Original Bridge.exe"], Path("C:/config.toml"), Path("C:/project"))
        report = actions.register_codex(client, target)
        self.assertIsNotNone(report["backup"])
        self.assertTrue(target.read_text().startswith(original.rstrip()))
        values = tomllib.loads(target.read_text())
        self.assertEqual(values["mcp_servers"]["other"]["command"], "original")
        changed = backend.client_configuration(["C:/New Bridge.exe"], Path("C:/config.toml"), Path("C:/project"))
        actions.register_codex(changed, target)
        values = tomllib.loads(target.read_text())
        self.assertEqual(values["mcp_servers"]["wiiu"]["command"], "C:/New Bridge.exe")
        self.assertEqual(values["model"], "original-fixture")

    def test_external_codex_entry_is_not_overwritten(self):
        target = self.root / "config.toml"
        target.write_text('[mcp_servers.wiiu]\ncommand = "external original fixture"\n')
        original = target.read_bytes()
        client = backend.client_configuration(["different"], Path("config"), Path("project"))
        with self.assertRaises(ValueError):
            actions.register_codex(client, target)
        self.assertEqual(target.read_bytes(), original)


class WindowsPackageTests(unittest.TestCase):
    def inputs(self, root):
        files = {"original.txt": b"original source fixture"}
        manifest = {"kind": "ctr2wup-wiiu-mcp-kit", "version": "0.1.0-alpha.2", "source_commit": "a" * 40,
                    "python_runtime_bundled": False, "python_dependencies_install_requires_internet": True,
                    "files": {n: {"bytes": len(d), "sha256": hashlib.sha256(d).hexdigest()} for n, d in files.items()}}
        base = root / "base.zip"
        with zipfile.ZipFile(base, "w") as archive:
            for name, data in files.items():
                archive.writestr(name, data)
            archive.writestr("manifest.json", json.dumps(manifest))
            archive.writestr("SHA256SUMS.txt", "fixture")
        frozen = root / "frozen"
        (frozen / "bin").mkdir(parents=True)
        (frozen / "licenses").mkdir()
        (frozen / "licenses/original.txt").write_text("original notice")
        records = {}
        for name in ("WiiU-MCP-Bridge.exe", "Install Wii U MCP.exe"):
            data = b"MZ original non-executable fixture " + name.encode()
            (frozen / "bin" / name).write_bytes(data)
            records[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        (frozen / "windows-build.json").write_text(json.dumps({"source_commit": "a" * 40,
            "architecture": "windows-x64", "files": records}))
        return base, frozen

    def test_combined_windows_inventory_and_checksums(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base, frozen = self.inputs(root)
            report = windows_package.package(base, frozen, root / "publish")
            self.assertTrue(report["python_bundled"])
            with zipfile.ZipFile(report["archive"]) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                self.assertTrue(manifest["python_runtime_bundled"])
                self.assertFalse(manifest["python_dependencies_install_requires_internet"])
                self.assertIn("Install Wii U MCP.exe", manifest["files"])
                self.assertIn("runtime/WiiU-MCP-Bridge.exe", manifest["files"])
                for name, record in manifest["files"].items():
                    self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), record["sha256"])

    def test_mismatched_frozen_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base, frozen = self.inputs(root)
            report = json.loads((frozen / "windows-build.json").read_text())
            report["source_commit"] = "b" * 40
            (frozen / "windows-build.json").write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                windows_package.package(base, frozen, root / "publish")
            self.assertFalse((root / "publish").exists())

    def test_changed_executable_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            base, frozen = self.inputs(root)
            (frozen / "bin/WiiU-MCP-Bridge.exe").write_bytes(b"MZchanged original fixture")
            with self.assertRaises(ValueError):
                windows_package.package(base, frozen, root / "publish")
            self.assertFalse((root / "publish").exists())


if __name__ == "__main__":
    unittest.main()
