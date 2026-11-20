"""Signed commands for the optional Aroma control plugin, over the existing SD transport."""

import ftplib
from hashlib import sha256
from io import BytesIO
import os
from pathlib import Path
import re
import struct
import tempfile
import time
import uuid

from .errors import HostError

ROOT = "ctr2wup/control"
APP_ROOT = "wiiu/apps/ctr2wup-control"
HEADER = struct.Struct(">4sQIBBHHHQ16s")
ENTRY = struct.Struct(">128sQ32s")
TITLE_ID = re.compile(r"00050000[0-9a-f]{8}\Z")
TITLE_FILE = re.compile(r"(?:code|content|meta)/(?:[A-Za-z0-9._-]+/)*[A-Za-z0-9._-]+\Z")
PACKAGE_FILE = re.compile(r"(?:[0-9a-f]{8}\.app|title\.(?:tmd|tik|cert))\Z")
STATE = re.compile(rb"CTR1 ([0-9a-f]{16}) ([0-9a-f]{8}) ([0-9a-f]{16}) ([0-9a-f]{64}) ([01]) ([0-9a-f]{32}|-)\n\Z")
RESULT = re.compile(rb"CTR1 ([0-9a-f]{16}) ([0-9a-f]{8}) ([0-9a-f]{32}) ([a-z_]+) (-?[0-9]+)\n\Z")
JOB = re.compile(r"[0-9a-f]{32}\Z")
REBOOT, HOMEBREW, TITLE, INSTALL = range(1, 5)
MAX_ENTRIES = 64
MAX_PACKAGE = 1024**3


def _key_type():
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    except ImportError as error:
        raise HostError("control_crypto_missing", "Install ctr2wup[packaging,mcp] for console control", stage="control") from error
    return Ed25519PrivateKey


def _read_key(path):
    with path.open("rb") as stream:
        raw = stream.read(33)
    if len(raw) != 32:
        raise ValueError("The local console control key must contain exactly 32 bytes")
    return _key_type().from_private_bytes(raw)


def pair(path):
    """Create or reuse a local key and export only its public half for SD."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        key = _read_key(path)
    else:
        key = _key_type().generate()
        with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0), 0o600), "wb") as stream:
            stream.write(key.private_bytes_raw())
    public = path.with_name("ctr2wup-control.pub")
    value = key.public_key().public_bytes_raw()
    if public.is_symlink():
        raise ValueError("The public key destination must be a regular file")
    if public.exists():
        if public.read_bytes() != value:
            raise ValueError("The existing public key does not match the private key")
    else:
        with public.open("xb") as stream:
            stream.write(value)
    return {"public_file": str(public), "sd_path": "wiiu/ctr2wup-control.pub", "pairing_id": value.hex()}


def _digest(stream, limit):
    hash_ = sha256()
    size = 0
    for block in iter(lambda: stream.read(256 * 1024), b""):
        size += len(block)
        if size > limit:
            raise ValueError("Control file exceeds its verified length or transfer limit")
        hash_.update(block)
    return size, hash_.digest()


def _entry(name, length, digest):
    encoded = name.encode("ascii")
    if not encoded or len(encoded) >= 128 or length > MAX_PACKAGE:
        raise ValueError("Install inventory contains an unsupported path or file size")
    return ENTRY.pack(encoded, length, digest)


def _command(key, state, operation, job, *, target=0, title=0, version=0, files=(), installed=()):
    if len(files) > MAX_ENTRIES or len(installed) > MAX_ENTRIES:
        raise ValueError("Console control supports up to 64 package files and 64 installed files")
    header = HEADER.pack(b"CTR1", int(state["session"], 16), int(state["sequence"], 16),
                         operation, target, len(files), len(installed), version, title, bytes.fromhex(job))
    message = header + b"".join(_entry(*file) for file in (*files, *installed))
    return key.sign(message) + message


class ConsoleControl:
    def __init__(self, bridge, key_path: Path):
        self.bridge = bridge
        self.key_path = key_path

    def _read(self, sd_path):
        bridge = self.bridge
        absolute = bridge._remote(sd_path)
        with bridge._ftp() as ftp:
            item = bridge._entry(ftp, absolute)
            if item is None:
                return None
            if item["type"] != "file" or item["size"] > 512:
                raise ValueError("Console control response must be a bounded file")
            try:
                return bridge._read(ftp, absolute, item["size"], 512)
            except ValueError:
                # The console may be replacing a receipt while FTP reads it.
                return b""

    def state(self):
        document = self._read(ROOT + "/state.txt")
        result = STATE.fullmatch(document or b"")
        if not result:
            raise HostError("control_unavailable", "The Aroma control plugin has not published a state record. Check its installation and public key", stage="control")
        session, sequence, title, public, menu, homebrew = (field.decode("ascii") for field in result.groups())
        key = _read_key(self.key_path)
        if key.public_key().public_bytes_raw().hex() != public:
            raise HostError("control_pair_mismatch", "The console control plugin has a different public key", stage="control")
        return {"session": session, "sequence": sequence, "title_id": title, "wiiu_menu": menu == "1",
                "homebrew_operation": None if homebrew == "-" else homebrew}

    def result(self, job):
        if not isinstance(job, str) or not JOB.fullmatch(job):
            raise ValueError("Expected the 32-character console operation ID")
        data = self._read(f"{ROOT}/result-{job}.txt")
        if data is None:
            return {"operation_id": job, "state": "awaiting_console"}
        result = RESULT.fullmatch(data)
        if not result or result[3].decode("ascii") != job:
            return {"operation_id": job, "state": "receipt_unreadable"}
        session, sequence, _, stage, code = (field.decode("ascii") for field in result.groups())
        return {"operation_id": job, "session": session, "sequence": sequence, "state": stage, "native_result": int(code)}

    def _upload(self, sd_path, stream, size, expected, *, verify_readback=True):
        """Stage a control file and publish it after checking the transfer."""
        bridge = self.bridge
        absolute = bridge._remote(sd_path, write=True)
        temporary = absolute + ".part"
        with bridge._ftp() as ftp:
            if bridge._entry(ftp, absolute, create_parents=True) is not None or bridge._entry(ftp, temporary) is not None:
                raise ValueError("Console control staging path already exists")
            ftp.storbinary("STOR " + temporary, stream, blocksize=128 * 1024)
            if verify_readback:
                hash_ = sha256()
                received = 0

                def receive(block):
                    nonlocal received
                    received += len(block)
                    if received > size:
                        raise ValueError("Control upload read-back exceeds the expected size")
                    hash_.update(block)

                ftp.retrbinary("RETR " + temporary, receive, blocksize=128 * 1024)
                if received != size or hash_.digest() != expected:
                    raise ValueError("Control upload read-back differs")
            else:
                staged = bridge._entry(ftp, temporary)
                if staged is None or staged["type"] != "file" or staged["size"] != size:
                    raise ValueError("Staged control file size differs")
            if bridge._entry(ftp, absolute) is not None:
                raise ValueError("Control destination appeared during upload")
            ftp.rename(temporary, absolute)

    def _submit(self, state, operation, job, *, wait_seconds=10, **arguments):
        request = _command(_read_key(self.key_path), state, operation, job, **arguments)
        sd_path = f"{ROOT}/req-{state['session']}-{state['sequence']}.bin"
        try:
            self._upload(sd_path, BytesIO(request), len(request), sha256(request).digest())
        except (OSError, EOFError, ftplib.Error):
            return {"operation_id": job, "state": "submission_unknown"}
        deadline = time.monotonic() + wait_seconds
        while True:
            try:
                result = self.result(job)
            except (OSError, EOFError, ftplib.Error):
                # A reboot/title transition can close FTP right after submission.
                result = {"operation_id": job, "state": "awaiting_console"}
            except ValueError:
                result = {"operation_id": job, "state": "receipt_unreadable"}
            if result["state"] not in ("awaiting_console", "receipt_unreadable"):
                if (result["session"], result["sequence"]) != (state["session"], state["sequence"]):
                    return {"operation_id": job, "state": "receipt_unreadable"}
                return result
            if time.monotonic() >= deadline:
                return result
            time.sleep(0.4)

    def reboot(self):
        return self._submit(self.state(), REBOOT, uuid.uuid4().hex)

    def _observe_launch(self, state, *, key, value):
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            try:
                current = self.state()
            except (HostError, ValueError, OSError, EOFError, ftplib.Error):
                pass
            else:
                if current["session"] != state["session"] and current[key] == value:
                    return True
            time.sleep(0.4)
        return False

    def launch_homebrew(self, suffix, data):
        state = self.state()
        job = uuid.uuid4().hex
        name = job + suffix
        digest = sha256(data).digest()
        self._upload(f"{APP_ROOT}/{name}", BytesIO(data), len(data), digest)
        result = self._submit(state, HOMEBREW, job, files=[(name, len(data), digest)])
        observed = result["state"] in ("launch_requested", "awaiting_console", "submission_unknown") and (
            self._observe_launch(state, key="homebrew_operation", value=job))
        return {**result, "execution_confirmed": observed}

    def launch_title(self, title_id):
        _check_title(title_id)
        state = self.state()
        result = self._submit(state, TITLE, uuid.uuid4().hex, title=int(title_id, 16))
        observed = result["state"] == "launch_requested" and self._observe_launch(state, key="title_id", value=title_id)
        return {**result, "title_id": title_id, "execution_confirmed": observed}

    def install(self, path: Path, title_id, target):
        from .package_integrity import verify_package

        _check_title(title_id)
        if target not in ("mlc", "usb"):
            raise ValueError("Choose mlc or usb as the installation target")
        state = self.state()
        if not state["wiiu_menu"]:
            raise ValueError("Return to the Wii U Menu before installing a WUP")
        files = sorted(p for p in path.iterdir() if p.name != "pack-report.json")
        if not 7 <= len(files) <= MAX_ENTRIES:
            raise ValueError("The WUP folder must contain a supported package inventory")
        for item in files:
            if not PACKAGE_FILE.fullmatch(item.name) or item.is_symlink() or not item.is_file():
                raise ValueError("The WUP folder contains an unexpected package file")
        if sum(item.stat().st_size for item in files) > MAX_PACKAGE:
            raise ValueError("The WUP exceeds the 1 GiB control install limit")

        # Verify a private copy and stage those exact bytes. The caller can keep
        # building new versions without racing this installation.
        job = uuid.uuid4().hex
        with tempfile.TemporaryDirectory(prefix="wup-", dir=self.key_path.parent) as temporary:
            root = Path(temporary)
            package_files = []
            copied_bytes = 0
            for item in files:
                copy = root / item.name
                with item.open("rb") as source, copy.open("xb") as destination:
                    for block in iter(lambda: source.read(256 * 1024), b""):
                        copied_bytes += len(block)
                        if copied_bytes > MAX_PACKAGE:
                            raise ValueError("The WUP changed size while preparing the installation")
                        destination.write(block)
                with copy.open("rb") as stream:
                    size, digest = _digest(stream, MAX_PACKAGE)
                package_files.append((item.name, size, digest))
            report = verify_package(root, require_menu=True, require_startup=True)
            if report["title_id"] != title_id or report["validation"] != "plaintext_integrity":
                raise ValueError("The checked homebrew WUP does not match the requested title ID")
            if report["menu_profile"] != "wup-menu-v3":
                raise ValueError("The WUP requires the verified Wii U Menu startup profile")
            installed = [(item["path"], item["bytes"], bytes.fromhex(item["sha256"])) for item in report["files"]]
            if len(installed) > MAX_ENTRIES or sum(item[1] for item in installed) > MAX_PACKAGE:
                raise ValueError("The installed file inventory exceeds the control limits")
            for name, size, digest in installed:
                if not TITLE_FILE.fullmatch(name) or any(part in (".", "..") for part in name.split("/")):
                    raise ValueError("The WUP contains an unsupported installed file path")
                _entry(name, size, digest)
            for name, size, digest in package_files:
                with (root / name).open("rb") as stream:
                    # The signed install request carries these hashes. The
                    # console checks the staged inventory before submitting it.
                    self._upload(f"install/ctr2wup-{job}/{name}", stream, size, digest,
                                 verify_readback=False)
            try:
                current = self.state()
            except (HostError, ValueError, OSError, EOFError, ftplib.Error):
                current = None
            if current != state:
                result = {"operation_id": job, "state": "staged_only"}
            else:
                result = self._submit(state, INSTALL, job, wait_seconds=15, target=int(target == "usb"),
                                      title=int(title_id, 16), version=report["title_version"],
                                      files=package_files, installed=installed)
        return {**result, "title_id": title_id, "target": target, "staged_sd_path": "install/ctr2wup-" + job}


def _check_title(title_id):
    if not isinstance(title_id, str) or not TITLE_ID.fullmatch(title_id) or title_id.endswith("00000000"):
        raise ValueError("Expected a lowercase Wii U application title ID beginning 00050000")
