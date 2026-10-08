"""Small private executable view for attachment-size-limited title intake."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile

from .atomic import publish_directory
from .cia import read_cia
from .containers import Reader, inspect, _ncch, u32
from .errors import HostError

MAX_VIEW = 30 * 1024 * 1024


def export_intake(path, output):
    """Never export tickets/certificates/keys/RomFS; never alter the input."""
    path = Path(path).absolute()
    output = Path(output).absolute()
    if path.is_symlink() or not path.is_file():
        raise HostError("unsafe_input", "Intake requires a regular input file")
    if output.exists() or output.is_symlink():
        raise HostError("output_exists", "Intake output already exists; it was preserved")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".ctr2wup-export-", dir=output.parent))
    try:
        with path.open("rb") as file:
            reader = Reader(file)
            prefix = reader.read(0, min(reader.size, 0x200), "intake header")
            cia = read_cia(reader) if len(prefix) >= 4 and u32(prefix, 0) == 0x2020 else None
            digest = reader.digest(0, reader.size)
            after = os.fstat(file.fileno())
            if (reader.initial_stat.st_size, reader.initial_stat.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise HostError("input_changed", "Source changed while preparing intake")
        report = {"schema_version": 1, "stage": "prepare-intake", "source_sha256": digest,
                  "source_bytes": reader.size, "cia": cia, "profile_enabled": False,
                  "console_verified": False, "romfs_exported": False,
                  "credentials_exported": False, "executable_view": None}
        try:
            checked = inspect(path)
        except HostError as exc:
            if exc.code not in ("encrypted_cia_content", "encrypted_input", "unsupported_cia_title", "missing_cia_executable"):
                raise
            report.update(ok=True, executable_available=False, blocking_error=exc.as_dict()["error"])
        else:
            if checked["input_sha256"] != digest:
                raise HostError("input_changed", "Source changed after intake metadata inspection")
            main = checked["partitions"][0]
            with path.open("rb") as file:
                reader = Reader(file)
                header = bytearray(reader.read(main["offset"], 0x200, "NCCH header"))
                unit = 0x200 << header[0x18E]
                # Keep original offsets/layout for code and header identity;
                # remove RomFS/plain/logo declarations and payloads explicitly.
                exefs = main["sections"]["exefs"]
                length = (exefs["offset"] - main["offset"]) + exefs["size"]
                if length > MAX_VIEW:
                    raise HostError("intake_size_limit", "Executable view exceeds 30 MiB; metadata remains available via CIA inspection")
                data = bytearray(length)
                header[0x18F] |= 2
                struct.pack_into("<I", header, 0x104, length // unit)
                header[0x130:0x150] = bytes(32)
                header[0x190:0x1A0] = bytes(16)
                header[0x1B0:0x1C0] = bytes(16)
                header[0x1E0:0x200] = bytes(32)
                header[:0x100] = bytes(0x100)  # derived view has no authentic header signature
                data[:0x200] = header
                data[0x200:0xA00] = reader.read(main["offset"] + 0x200, 0x800, "extended header/access descriptor")
                location = exefs["offset"] - main["offset"]
                data[location:location + exefs["size"]] = reader.read(exefs["offset"], exefs["size"], "ExeFS")
                if reader.digest(0, reader.size) != digest:
                    raise HostError("input_changed", "Source changed during executable export")
            view = stage / "executable-view.cxi"
            view.write_bytes(data)
            inspected = inspect(view)
            if inspected["executable_revision"] != checked["executable_revision"]:
                raise HostError("intake_revision_mismatch", "Derived view differs from the original executable revision")
            report.update(ok=True, executable_available=True, identity=checked["identity"],
                          executable_revision=checked["executable_revision"],
                          executable_view={"name": view.name, "sha256": hashlib.sha256(data).hexdigest(),
                                           "bytes": len(data), "derived_not_full_rom": True,
                                           "signature_authenticity_verified": False},
                          inspection=checked)
        (stage / "intake-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n")
        archive = stage / "CTR2WUP-intake.zip"
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
            bundle.write(stage / "intake-report.json", "intake-report.json")
            if report["executable_available"]:
                bundle.write(stage / "executable-view.cxi", "executable-view.cxi")
        if archive.stat().st_size >= 32 * 1024 * 1024:
            raise HostError("intake_size_limit", "Intake archive exceeds the attachment transfer limit")
        publish_directory(stage, output)
        return {**report, "output": str(output), "upload_archive": str(output / archive.name)}
    finally:
        if stage.exists(): shutil.rmtree(stage)
