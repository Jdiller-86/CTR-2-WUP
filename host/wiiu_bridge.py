"""Small Aroma bridge using FTPiiU and the public wiiload protocol.

No resident agent, custom wire protocol, shell commands or implicit retries.
File transport receipts are deliberately separate from hardware test verdicts.
"""
from __future__ import annotations

from collections import deque
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
import ftplib
import hashlib
import io
import ipaddress
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import socket
import subprocess
import tempfile
import threading
import tomllib
import uuid

from .doctor import tool_environment

SD_ROOT = "/fs/vol/external01"
MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 256 * 1024 * 1024
MAX_ENTRIES = 1024
MAX_DEPTH = 12
SUPPORTED_BUILDS = {
    "prototype": {}, "audio-check": {"audio_only": True},
    "audio-stream-check": {"stream_audio": True},
    "guest-audio": {"guest_audio": True}, "scene-check": {"scene_only": True},
    "m1-check": {"m1_only": True}, "install-check": {"install_only": True},
}


def relative_path(value: str) -> str:
    """Require a literal, portable relative path; never normalize traversal away."""
    if (not isinstance(value, str) or not value or len(value) > 240
            or any(not part or any(ord(c) < 32 or ord(c) == 127 or c in '<>:"|?*\\' for c in part)
                   or part in (".", "..") or part.endswith((" ", "."))
                   or re.fullmatch(r"CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]", part.split(".")[0], re.IGNORECASE)
                   for part in value.split("/"))):
        raise ValueError("Use a relative path with valid file names and no traversal")
    return value


def local_path(root: Path, value: str) -> Path:
    relative_path(value)
    result = root
    for part in value.split("/"):
        result = result / part
        if result.is_symlink():
            raise ValueError("Local paths must not traverse symbolic links")
    if not result.resolve().is_relative_to(root.resolve()):
        raise ValueError("Local path escapes its configured root")
    return result


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


@dataclass(frozen=True)
class BridgeConfig:
    console_host: str
    project_root: Path
    ftp_port: int = 21
    timeout: float = 10
    wiiload: str = "builtin"
    read_roots: tuple[str, ...] = ("ctr2wup", "wiiu/apps", "wiiu/crash_logs", "wiiu/screenshots", "install")
    write_roots: tuple[str, ...] = ("ctr2wup", "wiiu/apps", "install")

    def __post_init__(self):
        # A numeric address binds FTP, wiiload and UDP filtering to one peer.
        ipaddress.IPv4Address(self.console_host)
        if type(self.ftp_port) is not int or not 1 <= self.ftp_port <= 65535:
            raise ValueError("ftp_port must be between 1 and 65535")
        if isinstance(self.timeout, bool) or not 0 < self.timeout <= 60:
            raise ValueError("timeout must be greater than zero and at most 60 seconds")
        if not self.project_root.is_absolute() or self.project_root.is_symlink():
            raise ValueError("project_root must be an absolute real directory")
        if not (self.project_root / "runtime/wiiu/Makefile").is_file():
            raise ValueError("project_root must identify the CTR-2-WUP checkout")
        if not self.wiiload or any(c in self.wiiload for c in "\r\n\0"):
            raise ValueError("wiiload must name the official local executable")
        for roots in (self.read_roots, self.write_roots):
            if not isinstance(roots, (list, tuple)) or not roots:
                raise ValueError("SD roots must be a nonempty list")
            for root in roots:
                relative_path(root)

    @classmethod
    def load(cls, path: str | Path) -> BridgeConfig:
        with Path(path).open("rb") as stream:
            document = tomllib.load(stream)
        unknown = document.keys() - cls.__dataclass_fields__.keys()
        if unknown:
            raise ValueError("Unknown bridge configuration fields")
        document["project_root"] = Path(document["project_root"]).expanduser()
        return cls(**document)


class WiiUBridge:
    def __init__(self, config: BridgeConfig):
        self.config = config
        self._operations = threading.Lock()
        self._logs = UDPLogs(config.console_host)

    def close(self):
        self._logs.close()

    def capabilities(self) -> dict:
        return {
            "transport": "Aroma FTPiiU + wiiload", "mcp_transport": "stdio",
            "console_host": self.config.console_host, "hardware_verified": False,
            "supported": ["build", "list_sd", "read_sd", "upload_sd_verified",
                          "download_sd_verified", "collect_results", "launch_homebrew", "udp_logs"],
            "unavailable": ["controller_injection", "live_memory_debugging", "trigger_screenshot",
                            "installed_title_launch", "automatic_wup_install", "remote_power_recovery"],
            "read_roots": list(self.config.read_roots), "write_roots": list(self.config.write_roots),
            "build_targets": list(SUPPORTED_BUILDS),
            "limits": {"file_bytes": MAX_FILE, "collection_bytes": MAX_TOTAL, "entries": MAX_ENTRIES},
            "evidence": "Transfers and launch submission do not establish test completion or Menu return",
        }

    def _remote(self, value: str, *, write: bool = False) -> str:
        relative_path(value)
        roots = self.config.write_roots if write else self.config.read_roots
        if not any(value == root or value.startswith(root + "/") for root in roots):
            raise ValueError("SD path is outside the configured access roots")
        return SD_ROOT + "/" + value

    @contextmanager
    def _ftp(self):
        ftp = ftplib.FTP(timeout=self.config.timeout)
        try:
            ftp.connect(self.config.console_host, self.config.ftp_port)
            ftp.login(os.environ.get("CTR2WUP_FTP_USER", ""), os.environ.get("CTR2WUP_FTP_PASSWORD", ""))
            ftp.set_pasv(True)
            yield ftp
        finally:
            # Close locally even after a broken control/data connection.
            ftp.close()

    @staticmethod
    def _listing(ftp, absolute: str) -> list[dict]:
        rows, seen = [], set()

        def receive(line):
            facts_text, separator, name = line.partition(" ")
            if not separator:
                raise ValueError("Invalid MLSD entry")
            facts = dict(item.split("=", 1) for item in facts_text.rstrip(";").split(";"))
            facts = {key.lower(): value for key, value in facts.items()}
            kind = facts.get("type", "").lower()
            if kind in ("cdir", "pdir"):
                return
            relative_path(name)
            if "/" in name or name in seen or len(rows) >= MAX_ENTRIES:
                raise ValueError("Invalid, duplicate or oversized SD directory listing")
            seen.add(name)
            size = facts.get("size", "0")
            if not size.isdecimal():
                raise ValueError("Invalid SD file size")
            rows.append({"name": name, "type": kind, "size": int(size), "modify": facts.get("modify")})

        # FTPiiU implements MLSD. No ambiguous parsing of human LIST output.
        ftp.retrlines("MLSD " + absolute, receive)
        return sorted(rows, key=lambda row: row["name"])

    def _entry(self, ftp, absolute: str, *, create_parents: bool = False):
        parts = absolute.removeprefix(SD_ROOT + "/").split("/")
        parent = SD_ROOT
        for index, part in enumerate(parts):
            row = next((r for r in self._listing(ftp, parent) if r["name"] == part), None)
            if index == len(parts) - 1:
                return row
            parent += "/" + part
            if row is None and create_parents:
                ftp.mkd(parent)
                row = {"type": "dir"}
            if row is None or row["type"] != "dir":
                raise ValueError("Missing or non-directory SD parent")
        raise ValueError("Missing SD path")

    @staticmethod
    def _read(ftp, absolute: str, expected_size: int, limit: int = MAX_FILE) -> bytes:
        if not 0 <= expected_size <= limit:
            raise ValueError("SD file exceeds the transfer limit")
        data = bytearray()

        def receive(block):
            if len(data) + len(block) > min(limit, expected_size):
                raise ValueError("SD file changed size or exceeds the transfer limit")
            data.extend(block)

        ftp.retrbinary("RETR " + absolute, receive)
        if len(data) != expected_size:
            raise ValueError("Incomplete SD transfer or changing file")
        return bytes(data)

    def status(self) -> dict:
        with self._operations, self._ftp() as ftp:
            ftp.voidcmd("NOOP")
            self._listing(ftp, SD_ROOT)
        return {"ftp_reachable": True, "sd_readable": True,
                "wiiload_available": self.config.wiiload == "builtin" or bool(self._wiiload()),
                "hardware_test_result": "unverified"}

    def list_files(self, sd_path: str) -> dict:
        absolute = self._remote(sd_path)
        with self._operations, self._ftp() as ftp:
            row = self._entry(ftp, absolute)
            if row is None or row["type"] != "dir":
                raise ValueError("SD path is not an existing directory")
            return {"sd_path": sd_path, "entries": self._listing(ftp, absolute)}

    def read_text(self, sd_path: str) -> dict:
        absolute = self._remote(sd_path)
        with self._operations, self._ftp() as ftp:
            row = self._entry(ftp, absolute)
            if row is None or row["type"] != "file":
                raise ValueError("SD path is not a regular file")
            data = self._read(ftp, absolute, row["size"], 64 * 1024)
        return {"sd_path": sd_path, "bytes": len(data), "sha256": digest(data),
                "text": data.decode("utf-8", errors="replace"), "test_verdict": "not_evaluated"}

    def upload(self, local_file: str, sd_path: str) -> dict:
        absolute = self._remote(sd_path, write=True)
        source = local_path(self.config.project_root, local_file)
        if not source.is_file() or source.stat().st_size > MAX_FILE:
            raise ValueError("Local source must be a regular file within the transfer limit")
        with source.open("rb") as stream:
            data = stream.read(MAX_FILE + 1)
        if len(data) > MAX_FILE:
            raise ValueError("Local source exceeds the transfer limit")
        temporary = str(PurePosixPath(absolute).with_name(".ctr2wup-" + uuid.uuid4().hex + ".part"))
        with self._operations, self._ftp() as ftp:
            if self._entry(ftp, absolute, create_parents=True) is not None:
                raise ValueError("SD destination exists; use a distinct build directory or filename")
            try:
                ftp.storbinary("STOR " + temporary, io.BytesIO(data))
                returned = self._read(ftp, temporary, len(data))
                if digest(returned) != digest(data):
                    raise ValueError("SD upload read-back hash differs")
                if self._entry(ftp, absolute) is not None:
                    raise ValueError("SD destination appeared during upload")
                ftp.rename(temporary, absolute)
                # Verify the published name too, not just its staging object.
                returned = self._read(ftp, absolute, len(data))
                if digest(returned) != digest(data):
                    raise ValueError("Published SD file hash differs")
            except BaseException:
                # Remove only our unique staging object; preserve final files
                # if publication succeeded but its response/read-back was lost.
                try:
                    ftp.delete(temporary)
                except (OSError, ftplib.Error):
                    pass
                raise
        return {"sd_path": sd_path, "bytes": len(data), "sha256": digest(data), "readback_verified": True}

    def download(self, sd_path: str, output_file: str) -> dict:
        absolute = self._remote(sd_path)
        target = local_path(self.config.project_root, "private/wiiu/" + relative_path(output_file))
        if target.exists():
            raise ValueError("Local destination exists; preserve the previous evidence")
        with self._operations, self._ftp() as ftp:
            row = self._entry(ftp, absolute)
            if row is None or row["type"] != "file":
                raise ValueError("SD path is not a regular file")
            data = self._read(ftp, absolute, row["size"])
            # A second read detects ordinary in-progress writes of equal size.
            if digest(self._read(ftp, absolute, row["size"])) != digest(data):
                raise ValueError("SD file is still changing; collect after the application closes it")
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
        return {"local_file": str(target), "sd_path": sd_path, "bytes": len(data),
                "sha256": digest(data), "two_reads_match": True, "test_verdict": "not_evaluated"}

    def collect(self, sd_path: str) -> dict:
        """Snapshot a bounded folder without overwriting old attempts or grading it."""
        absolute = self._remote(sd_path)
        output_name = "collection-" + uuid.uuid4().hex
        output = local_path(self.config.project_root, "private/wiiu/" + output_name)
        output.mkdir(parents=True)
        receipt = {"schema_version": 1, "kind": "ctr2wup-ftp-collection", "started_at_utc": utc_now(),
                   "sd_path": sd_path, "transport_complete": False, "test_verdict": "not_evaluated", "files": []}
        total, count = 0, 0
        try:
            with self._operations, self._ftp() as ftp:
                row = self._entry(ftp, absolute)
                if row is None or row["type"] != "dir":
                    raise ValueError("SD collection root is not a directory")

                def visit(remote, relative, depth):
                    nonlocal total, count
                    if depth > MAX_DEPTH:
                        raise ValueError("SD collection exceeds the directory depth limit")
                    for row in self._listing(ftp, remote):
                        count += 1
                        if count > MAX_ENTRIES:
                            raise ValueError("SD collection exceeds the entry limit")
                        name = relative + row["name"]
                        path = remote + "/" + row["name"]
                        if row["type"] == "dir":
                            visit(path, name + "/", depth + 1)
                        elif row["type"] == "file":
                            total += row["size"]
                            if total > MAX_TOTAL:
                                raise ValueError("SD collection exceeds the byte limit")
                            data = self._read(ftp, path, row["size"])
                            if digest(self._read(ftp, path, row["size"])) != digest(data):
                                raise ValueError("SD evidence is still changing")
                            target = local_path(output, "sd-card/" + sd_path + "/" + name)
                            target.parent.mkdir(parents=True, exist_ok=True)
                            with target.open("xb") as stream:
                                stream.write(data)
                            receipt["files"].append({"path": target.relative_to(output).as_posix(),
                                                     "size": len(data), "sha256": digest(data)})
                        else:
                            raise ValueError("SD collection contains a link or unsupported file type")

                visit(absolute, "", 0)
            if not receipt["files"]:
                raise ValueError("SD collection contains no files")
            receipt["transport_complete"] = True
        finally:
            receipt["finished_at_utc"] = utc_now()
            with (output / "collection.json").open("x", encoding="utf-8") as stream:
                json.dump(receipt, stream, indent=2)
                stream.write("\n")
        return {"output": str(output), **receipt}

    def _wiiload(self):
        return shutil.which(self.config.wiiload, path=tool_environment().get("PATH"))

    def launch(self, local_file: str) -> dict:
        source = local_path(self.config.project_root, local_file)
        if source.suffix.lower() not in (".rpx", ".wuhb") or not source.is_file():
            raise ValueError("Launch requires a local RPX or WUHB file")
        if not 4 <= source.stat().st_size <= MAX_FILE:
            raise ValueError("Homebrew size exceeds the launch contract")
        with source.open("rb") as stream:
            data = stream.read(MAX_FILE + 1)
        if len(data) > MAX_FILE:
            raise ValueError("Homebrew exceeds the launch limit")
        if data[:4] != (b"WUHB" if source.suffix.lower() == ".wuhb" else b"\x7fELF"):
            raise ValueError("Homebrew header does not match its extension")
        if self.config.wiiload == "builtin":
            from .wiiload import send
            with self._operations:
                result = send(self.config.console_host, source.name, data)
            return {"submitted": True, "local_file": local_file, "sha256": digest(data),
                    "test_verdict": "not_evaluated", **result}
        executable = self._wiiload()
        if executable is None:
            raise ValueError("Install devkitPro wiiload or set its absolute path in the bridge configuration")
        env = tool_environment()
        env["WIILOAD"] = "tcp:" + self.config.console_host
        # wiiload reopens its input. Use a private copy so an ordinary rebuild
        # cannot change the payload after the receipt's hash was computed.
        staging_root = local_path(self.config.project_root, "private/wiiu")
        staging_root.mkdir(parents=True, exist_ok=True)
        with self._operations, tempfile.TemporaryDirectory(prefix=".launch-", dir=staging_root) as temporary:
            staged = Path(temporary) / source.name
            staged.write_bytes(data)
            result = subprocess.run([executable, str(staged)], env=env, cwd=self.config.project_root,
                                    capture_output=True, text=True, errors="replace", timeout=60, check=False)
        if result.returncode:
            raise RuntimeError("wiiload failed; launch state is unknown. Check console/logs before retrying")
        return {"submitted": True, "local_file": local_file, "sha256": digest(data),
                "execution_confirmed": False, "test_verdict": "not_evaluated"}

    def build(self, target: str) -> dict:
        if target not in SUPPORTED_BUILDS:
            raise ValueError("Unsupported original diagnostic target")
        # Reuse the project's verified compiler, source identity and manifest.
        from .build import prototype
        from .doctor import repository_root
        if repository_root().resolve() != self.config.project_root.resolve():
            raise ValueError("Run this bridge from its configured checkout or set CTR2WUP_PROJECT_ROOT")
        output = local_path(self.config.project_root, "artifacts/wiiu-mcp/build-" + uuid.uuid4().hex)
        with self._operations:
            return prototype(output, **SUPPORTED_BUILDS[target])

    def start_logs(self, port: int = 4405) -> dict:
        return self._logs.start(port)

    def read_logs(self, after: int = 0) -> dict:
        return self._logs.read(after)

    def stop_logs(self) -> dict:
        self._logs.close()
        return self._logs.read(0)


class UDPLogs:
    """Bounded, source-filtered diagnostic stream, never a reliable result channel."""
    def __init__(self, console_host: str):
        self.peer = console_host
        self.socket = None
        self.thread = None
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.lifecycle = threading.Lock()
        self.rows = deque(maxlen=128)
        self.sequence = 0
        self.error = None
        self.port = None

    def start(self, port: int) -> dict:
        if type(port) is not int or not 1 <= port <= 65535:
            raise ValueError("UDP port must be between 1 and 65535")
        with self.lifecycle:
            if self.socket is not None:
                raise ValueError("UDP capture is already running")
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                sock.bind(("0.0.0.0", port))
                sock.settimeout(0.2)
            except BaseException:
                sock.close()
                raise
            self.stop.clear()
            with self.lock:
                self.rows.clear()
                self.sequence = 0
                self.error = None
            self.socket, self.port = sock, port
            self.thread = threading.Thread(target=self._receive, args=(sock,), daemon=True)
            self.thread.start()
        return {"listening": True, "port": port, "peer": self.peer, "delivery": "best_effort"}

    def _receive(self, sock):
        while not self.stop.is_set():
            try:
                data, address = sock.recvfrom(4097)
            except socket.timeout:
                continue
            except OSError:
                if not self.stop.is_set():
                    with self.lock:
                        self.error = "UDP receiver failed"
                break
            if address[0] != self.peer:
                continue
            with self.lock:
                self.sequence += 1
                self.rows.append({"sequence": self.sequence, "received_at_utc": utc_now(),
                                  "text": data[:4096].decode("utf-8", errors="replace"),
                                  "possibly_truncated": len(data) > 4096})

    def read(self, after: int) -> dict:
        if type(after) is not int or after < 0:
            raise ValueError("Log cursor must be a nonnegative integer")
        with self.lock:
            rows = [row.copy() for row in self.rows if row["sequence"] > after]
            oldest = self.rows[0]["sequence"] if self.rows else self.sequence + 1
            return {"listening": self.socket is not None and not self.error, "port": self.port,
                    "next_cursor": self.sequence, "buffer_overrun": after < oldest - 1,
                    "error": self.error, "datagrams": rows, "delivery": "best_effort",
                    "test_verdict": "not_evaluated"}

    def close(self):
        with self.lifecycle:
            self.stop.set()
            if self.thread is not None:
                self.thread.join(timeout=1)
            if self.socket is not None:
                self.socket.close()
            self.socket = None
            self.thread = None
