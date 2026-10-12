#!/usr/bin/env python3
"""Opt-in attribution for future Codex commits, without changing authentication.

The primary author uses the publicly verified OpenAI-owned GitHub bot's noreply
identity. The trailer follows OpenAI's published Codex attribution format.
Installation changes repository-local Git configuration only and refuses to
disable another hook or replace a custom alias. No history is rewritten.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

AUTHOR = "Codex"
EMAIL = "199175422+chatgpt-codex-connector[bot]@users.noreply.github.com"
TRAILER = "Co-authored-by: Codex <noreply@openai.com>"
ALIAS = ('!f() { if command -v python3 >/dev/null 2>&1; then '
         'python3 tools/codex-attribution.py commit -- "$@"; else '
         'python tools/codex-attribution.py commit -- "$@"; fi; }; f')


def git(*args, check=True):
    return subprocess.run(["git", *args], text=True, encoding="utf-8",
                          capture_output=True, check=check)


def install():
    root = Path(git("rev-parse", "--show-toplevel").stdout.strip()).resolve()
    if root != Path(__file__).resolve().parents[1]:
        raise RuntimeError("Run the installer from this repository's checkout.")
    hook = root / ".githooks" / "prepare-commit-msg"
    if not hook.is_file():
        raise RuntimeError("The tracked prepare-commit-msg hook is missing.")
    configured = git("config", "--get", "core.hooksPath", check=False).stdout.strip()
    if configured and Path(configured).as_posix() != ".githooks":
        raise RuntimeError("Existing core.hooksPath preserved; integrate the opt-in hook manually.")
    if not configured:
        directory = Path(git("rev-parse", "--git-path", "hooks").stdout.strip())
        active = [p.name for p in directory.iterdir()
                  if p.is_file() and not p.name.endswith(".sample")]
        if active:
            raise RuntimeError("Existing hooks preserved: " + ", ".join(sorted(active)))
    previous_alias = git("config", "--get", "alias.codex-commit", check=False).stdout.rstrip("\n")
    if previous_alias and previous_alias != ALIAS:
        raise RuntimeError("Existing codex-commit alias preserved.")
    hook.chmod(hook.stat().st_mode | 0o111)
    git("config", "--local", "core.hooksPath", ".githooks")
    git("config", "--local", "alias.codex-commit", ALIAS)
    print("Installed repository-local opt-in hook. Use git codex-commit for Codex work.")


def message(path, source=None):
    if os.environ.get("CTR2WUP_CODEX_COMMIT") != "1" or source in ("merge", "squash"):
        return
    content = Path(path).read_text(encoding="utf-8")
    # Preserve other authors and metadata, while keeping our exact trailer once.
    lines = [line for line in content.splitlines() if line.strip() != TRAILER]
    clean = "\n".join(lines).rstrip()
    existing = subprocess.run(["git", "interpret-trailers", "--parse"], input=clean,
                              capture_output=True, text=True, encoding="utf-8", check=True)
    separator = "\n" if existing.stdout.strip() else "\n\n"
    Path(path).write_text(clean + separator + TRAILER + "\n", encoding="utf-8")


def commit(arguments):
    if arguments[:1] == ["--"]:
        arguments = arguments[1:]
    if any(arg == "--reset-author" or arg == "--author" or arg.startswith("--author=")
           for arg in arguments):
        raise RuntimeError("codex-commit supplies its verified author identity; use git commit for other authors.")
    if git("config", "--get", "core.hooksPath", check=False).stdout.strip() != ".githooks":
        raise RuntimeError("Install the opt-in hook first with the install command.")
    environment = os.environ.copy()
    environment.update(CTR2WUP_CODEX_COMMIT="1", GIT_AUTHOR_NAME=AUTHOR, GIT_AUTHOR_EMAIL=EMAIL)
    return subprocess.run(["git", "commit", *arguments], env=environment).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("install")
    hook = sub.add_parser("message")
    hook.add_argument("path")
    hook.add_argument("source", nargs="?")
    hook.add_argument("original_commit", nargs="?")
    wrapper = sub.add_parser("commit")
    wrapper.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        if args.command == "install":
            install()
        elif args.command == "message":
            message(args.path, args.source)
        else:
            return commit(args.arguments)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Codex attribution: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
