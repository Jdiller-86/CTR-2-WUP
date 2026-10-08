"""Local packaging inputs and AES primitives. No credentials are bundled."""
from __future__ import annotations

import json
from pathlib import Path
import re
import stat
import struct

from .errors import HostError


def fail(code, message):
    raise HostError(code, message, stage="packaging")


def cipher(key, iv):
    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError as exc:
        raise HostError("crypto_unavailable", "Install the documented ctr2wup[packaging] dependency", stage="packaging") from exc
    return Cipher(algorithms.AES(key), modes.CBC(iv))


def local_bytes(path, maximum, *, exact=None):
    """Read a bounded regular file; errors deliberately omit paths and contents."""
    path = Path(path)
    try:
        if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
            fail("invalid_packaging_input", "Local packaging inputs must be regular files")
        if path.stat().st_size > maximum:
            fail("invalid_packaging_input", "Local packaging input exceeds its supported size")
        with path.open("rb") as handle:
            data = handle.read(maximum + 1)
    except OSError as exc:
        raise HostError("unreadable_packaging_input", "A local packaging input could not be read", stage="packaging") from exc
    if len(data) > maximum or (exact is not None and len(data) != exact):
        fail("invalid_packaging_input", "Local packaging input has an invalid length")
    return data


def common_key(path):
    return local_bytes(path, 16, exact=16)


def certificate_subjects(data):
    """Parse Nintendo certificate *layout*, without authenticating a root."""
    signatures = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80,
                  0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}
    key_sizes = {0: 0x238, 1: 0x138, 2: 0x78}
    result = {}
    offset = 0

    def text_field(value):
        value, separator, padding = value.partition(b"\0")
        if not separator or any(padding):
            fail("invalid_certificate", "Certificate names must be bounded, NUL-terminated text")
        try:
            text = value.decode("ascii")
        except UnicodeDecodeError:
            fail("invalid_certificate", "Certificate names must be ASCII")
        if not re.fullmatch(r"[A-Za-z0-9-]{1,63}", text):
            fail("invalid_certificate", "Certificate names contain unsupported characters")
        return text

    while offset < len(data):
        if len(data) - offset < 4:
            fail("invalid_certificate", "Certificate chain is truncated")
        signature = struct.unpack_from(">I", data, offset)[0]
        body = offset + signatures.get(signature, len(data) + 1)
        if body + 0x88 > len(data):
            fail("invalid_certificate", "Certificate signature profile is unsupported or truncated")
        key_type = struct.unpack_from(">I", data, body + 0x40)[0]
        size = key_sizes.get(key_type)
        if size is None or body + 0x88 + size > len(data):
            fail("invalid_certificate", "Certificate public-key profile is unsupported or truncated")
        issuer = text_field(data[body:body + 0x40])
        name = text_field(data[body + 0x44:body + 0x84])
        subject = issuer + "-" + name
        if len(subject) > 63 or subject in result:
            fail("invalid_certificate", "Certificate subjects must be unique and fit issuer fields")
        result[subject] = {"issuer": issuer, "name": name, "key_type": key_type}
        offset = body + 0x88 + size
    if not result or len(result) > 16:
        fail("invalid_certificate", "Provide a bounded local certificate chain")
    for entry in result.values():
        if entry["issuer"] != "Root" and entry["issuer"] not in result:
            fail("invalid_certificate", "Certificate chain has a missing parent")
    return result


class PackagingInputs:
    """Secrets never appear in repr, reports, subprocess arguments or exceptions."""
    def __init__(self, config):
        config = Path(config).absolute()
        try:
            document = json.loads(local_bytes(config, 16384))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise HostError("invalid_packaging_config", "Packaging configuration must be valid JSON", stage="packaging") from exc
        expected = {"schema_version", "signature_policy", "common_key_file", "certificate_file"}
        if (not isinstance(document, dict) or set(document) != expected
                or document["schema_version"] != 1
                or document["signature_policy"] != "homebrew_unsigned"
                or any(not isinstance(document[name], str) or not document[name]
                       for name in ("common_key_file", "certificate_file"))):
            fail("invalid_packaging_config", "Use schema 1, homebrew_unsigned policy and local common_key_file/certificate_file paths")
        self.key_path = (config.parent / document["common_key_file"]).absolute()
        self.cert_path = (config.parent / document["certificate_file"]).absolute()
        self.key = common_key(self.key_path)
        self.certificates = local_bytes(self.cert_path, 16384)
        subjects = certificate_subjects(self.certificates)
        self.issuers = []
        for prefix in ("CP", "XS"):
            matches = [name for name, entry in subjects.items()
                       if entry["name"].startswith(prefix) and entry["key_type"] == 1]
            if len(matches) != 1:
                fail("invalid_certificate", "Provide exactly one RSA-2048 CP and XS signer certificate")
            self.issuers.append(matches[0])

    def __repr__(self):
        return "PackagingInputs(<local inputs redacted>)"
