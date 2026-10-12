"""Exercise real Git commits and hook preservation in isolated fixture repos."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EMAIL = "199175422+chatgpt-codex-connector[bot]@users.noreply.github.com"
TRAILER = "Co-authored-by: Codex <noreply@openai.com>"


@unittest.skipUnless(shutil.which("git"), "Git is required for hook integration")
class CodexAttributionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "fixture with spaces"
        (self.root / "tools").mkdir(parents=True)
        (self.root / ".githooks").mkdir()
        for name in ("tools/codex-attribution.py", ".githooks/prepare-commit-msg"):
            shutil.copy2(ROOT / name, self.root / name)
        self.run_command("git", "init", "-b", "main")
        self.run_command("git", "config", "user.name", "Human Fixture")
        self.run_command("git", "config", "user.email", "human@example.invalid")
        # Isolate the fixture from user/global hooks, aliases and author overrides.
        self.run_command("git", "config", "commit.gpgsign", "false")
        (self.root / "original.txt").write_text("original fixture\n", encoding="utf-8")
        self.run_command("git", "add", ".")

    def run_command(self, *args, **kwargs):
        environment = os.environ.copy()
        for key in ("GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL",
                    "CTR2WUP_CODEX_COMMIT"):
            environment.pop(key, None)
        environment.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        environment.update(kwargs.pop("environment", {}))
        return subprocess.run(args, cwd=self.root, env=environment, capture_output=True,
                              text=True, encoding="utf-8", **kwargs)

    def install(self):
        result = self.run_command(sys.executable, "tools/codex-attribution.py", "install")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_codex_alias_records_verified_author_and_official_trailer(self):
        self.install()
        result = self.run_command("git", "codex-commit", "-m", "Original change with 'quotes' and $(literal)")
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.run_command("git", "show", "-s", "--format=%an%n%ae%n%B").stdout
        self.assertIn("Codex\n" + EMAIL + "\n", record)
        self.assertIn("$(literal)", record)
        self.assertEqual(record.count(TRAILER), 1)
        self.assertEqual(self.run_command("git", "config", "user.name").stdout.strip(), "Human Fixture")

    def test_normal_commit_has_human_author_and_unchanged_message(self):
        self.install()
        result = self.run_command("git", "commit", "-m", "Human change")
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.run_command("git", "show", "-s", "--format=%an%n%ae%n%B").stdout
        self.assertIn("Human Fixture\nhuman@example.invalid\nHuman change\n", record)
        self.assertNotIn(TRAILER, record)

    def test_install_idempotent(self):
        self.install()
        self.install()

    def test_preserves_existing_hook_path(self):
        self.run_command("git", "config", "core.hooksPath", "custom-hooks")
        result = self.run_command(sys.executable, "tools/codex-attribution.py", "install")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.run_command("git", "config", "core.hooksPath").stdout.strip(), "custom-hooks")

    def test_preserves_active_hook_and_custom_alias(self):
        active = self.root / ".git/hooks/pre-commit"
        active.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        result = self.run_command(sys.executable, "tools/codex-attribution.py", "install")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(active.is_file())
        active.unlink()
        self.run_command("git", "config", "alias.codex-commit", "status")
        result = self.run_command(sys.executable, "tools/codex-attribution.py", "install")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.run_command("git", "config", "alias.codex-commit").stdout.strip(), "status")

    def test_attribution_deduplicates_without_losing_other_trailers(self):
        message = self.root / "message.txt"
        message.write_text("Change\n\nSigned-off-by: Human <human@example.invalid>\n" + TRAILER + "\n" + TRAILER + "\n")
        for _ in range(2):
            result = self.run_command(sys.executable, "tools/codex-attribution.py", "message", str(message),
                                      environment={"CTR2WUP_CODEX_COMMIT": "1"})
            self.assertEqual(result.returncode, 0, result.stderr)
        text = message.read_text()
        self.assertEqual(text.count(TRAILER), 1)
        self.assertIn("Signed-off-by: Human <human@example.invalid>\n" + TRAILER, text)
        self.assertIn("Change\n\nSigned-off-by:", text)

    def test_merge_message_and_disabled_hook_are_unchanged(self):
        message = self.root / "message.txt"
        original = "Merge existing branch\n"
        message.write_text(original)
        for source, environment in (("merge", {"CTR2WUP_CODEX_COMMIT": "1"}), ("message", {})):
            result = self.run_command(sys.executable, "tools/codex-attribution.py", "message", str(message), source,
                                      environment=environment)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(message.read_text(), original)

    def test_wrapper_refuses_identity_override(self):
        self.install()
        result = self.run_command(sys.executable, "tools/codex-attribution.py", "commit", "--", "--reset-author")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("verified author identity", result.stderr)
