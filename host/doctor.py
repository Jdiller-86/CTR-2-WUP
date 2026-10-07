"""Discover build prerequisites without printing environment or secret values."""

from __future__ import annotations

import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


def repository_root():
    override = os.environ.get("CTR2WUP_PROJECT_ROOT")
    if override:
        return Path(override).expanduser().resolve()
    source = Path(__file__).resolve().parents[1]
    if (source / "runtime/wiiu/Makefile").is_file():
        return source
    if (Path.cwd() / "runtime/wiiu/Makefile").is_file():
        return Path.cwd()
    return source


def tool_environment(source=None):
    env = dict(os.environ if source is None else source)
    # A relocatable local SDK installed by tools/install-wiiu-sdk.sh is preferred
    # only when no user-provided devkitPro binding exists.
    local = repository_root().parent / "ctr2wup-sdk"
    devkitpro = env.get("DEVKITPRO")
    if not devkitpro and local.joinpath("devkitPPC").is_dir():
        devkitpro = str(local)
    if not devkitpro and Path("/opt/devkitpro").is_dir():
        devkitpro = "/opt/devkitpro"
    if devkitpro:
        env["DEVKITPRO"] = devkitpro
        env.setdefault("DEVKITPPC", str(Path(devkitpro) / "devkitPPC"))
        entries = [str(Path(env["DEVKITPPC"]) / "bin"),
                   str(Path(devkitpro) / "tools" / "bin"),
                   str(Path(devkitpro) / "usr" / "bin")]
        env["PATH"] = os.pathsep.join(entries + [env.get("PATH", "")])
    return env


def _tool(name, env, version=True):
    path = shutil.which(name, path=env.get("PATH"))
    result = {"available": bool(path), "path": path}
    if path and version:
        try:
            completed = subprocess.run([path, "--version"], env=env,
                                       capture_output=True, text=True, timeout=10,
                                       errors="replace", check=False)
            result["version"] = (completed.stdout or completed.stderr).splitlines()[0][:300]
            result["version_exit_code"] = completed.returncode
        except (OSError, subprocess.SubprocessError, IndexError) as exc:
            result["version_error"] = type(exc).__name__
    return result


def doctor(source=None):
    env = tool_environment(source)
    tools = {name: _tool(name, env, version=name not in ("elf2rpl", "rpltool", "wuhbtool"))
             for name in ("make", "gcc", "g++", "powerpc-eabi-gcc", "powerpc-eabi-g++", "elf2rpl", "rpltool", "wuhbtool")}
    root = Path(env["DEVKITPRO"]) if env.get("DEVKITPRO") else None
    wut = root / "wut" if root else None
    wut_ok = bool(wut and (wut / "include" / "whb" / "gfx.h").is_file()
                  and (wut / "lib" / "libwut.a").is_file())
    required = ["make", "powerpc-eabi-gcc", "powerpc-eabi-g++", "elf2rpl", "wuhbtool"]
    missing = [name for name in required if not tools[name]["available"]]
    if not wut_ok:
        missing.append("WUT headers/library")
    if sys.version_info < (3, 12):
        missing.append("Python 3.12+")
    return {"schema_version": 1, "ok": not missing, "stage": "doctor",
            "host": {"system": platform.system(), "machine": platform.machine(),
                     "python": platform.python_version(), "python_executable": sys.executable},
            "tools": tools,
            "wut": {"available": wut_ok, "path": str(wut) if wut else None},
            "missing_required": missing,
            "next_action": "Install Python 3.12+, GNU make, devkitPPC, WUT and Wii U tools; set DEVKITPRO and DEVKITPPC" if missing else None,
            "console_validation": "awaiting physical Wii U", "rom_credentials_required": False}
