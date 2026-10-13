"""Shared portable guest runtime source manifest for build/validation adapters."""
from pathlib import Path
import re


def runtime_sources(repository: Path) -> list[Path]:
    core = repository / "runtime/core"
    manifest = core / "sources.list"
    if manifest.is_symlink() or not manifest.is_file() or manifest.stat().st_size > 4096:
        raise ValueError("Portable runtime source manifest must be a bounded regular file")
    names = manifest.read_text(encoding="ascii").splitlines()
    if not names or len(names) > 100 or len(set(names)) != len(names):
        raise ValueError("Portable runtime source manifest is empty or repeated")
    if any(not re.fullmatch(r"ctr_[a-z0-9_]+\.c", name) for name in names):
        raise ValueError("Portable runtime source manifest contains an invalid source name")
    sources = [core / name for name in names]
    if any(path.is_symlink() or not path.is_file() for path in sources):
        raise ValueError("Portable runtime source manifest contains an unavailable source")
    return sources
