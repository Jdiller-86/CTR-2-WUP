"""Bounded, local-only 3DS input keys. No platform key values are bundled."""
import json
import os
from pathlib import Path
import re
import stat

from .errors import HostError


def fail(code, message):
    raise HostError(code, message, stage="prepare-rom")


def local_bytes(path, limit, *, exact=None):
    """Do not disclose key contents or paths through diagnostics."""
    try:
        path = Path(path)
        if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
            fail("invalid_rom_key_file", "Key inputs must be bounded regular local files")
        with path.open("rb") as handle:
            if not stat.S_ISREG(os.fstat(handle.fileno()).st_mode):
                fail("invalid_rom_key_file", "Key inputs must be regular local files")
            data = handle.read(limit + 1)
    except OSError:
        fail("unreadable_rom_key_file", "A required local key input could not be read")
    if len(data) > limit or (exact is not None and len(data) != exact):
        fail("invalid_rom_key_file", "A local key input has an unsupported length")
    return data


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            fail("invalid_rom_key_config", "Key configuration contains duplicate fields")
        result[name] = value
    return result


class RomKeys:
    """Only file paths enter the configuration; values are loaded on demand."""
    ROLES = {"ncch_key_x_files": r"(?:2c|25|18|1b)",
             "cia_common_key_files": r"[0-5]",
             "cia_title_key_files": r"[0-9a-f]{16}",
             "title_seed_files": r"[0-9a-f]{16}"}

    def __init__(self, config):
        if config is None:
            fail("missing_rom_keys", "Encrypted input needs a local 3DS key configuration")
        self._base = Path(config).absolute().parent
        try:
            document = json.loads(local_bytes(config, 262144), object_pairs_hook=unique_object)
        except (UnicodeDecodeError, json.JSONDecodeError, RecursionError):
            fail("invalid_rom_key_config", "Key configuration must be valid bounded JSON")
        if (not isinstance(document, dict) or type(document.get("schema_version")) is not int
                or document["schema_version"] != 1
                or set(document) - ({"schema_version"} | self.ROLES.keys())):
            fail("invalid_rom_key_config", "Use key schema 1 with the documented file-path maps")
        self._maps = {}
        for role, pattern in self.ROLES.items():
            entries = document.get(role, {})
            if (not isinstance(entries, dict) or len(entries) > 2048
                    or any(not re.fullmatch(pattern, name) or not isinstance(value, str)
                           or not value or len(value) > 4096 or '\0' in value
                           for name, value in entries.items())):
                fail("invalid_rom_key_config", "Key references have an unsupported role, identity or path")
            self._maps[role] = entries

    def has(self, role, identity):
        return identity in self._maps[role]

    def read(self, role, identity):
        path = self._maps[role].get(identity)
        if path is None:
            fail("missing_rom_key", "Required 3DS key/seed is missing: " + role + " / " + identity)
        return local_bytes(self._base / path, 16, exact=16)

    def __repr__(self):
        return "RomKeys(<local inputs redacted>)"


def scramble(key_x, key_y):
    """3DS public key-generation algorithm; constant is not a platform key."""
    if len(key_x) != 16 or len(key_y) != 16:
        fail("invalid_rom_key_file", "AES key components must contain 16 bytes")
    mask = (1 << 128) - 1
    def rotate(value, bits):
        return ((value << bits) | (value >> (128 - bits))) & mask
    x, y = int.from_bytes(key_x, "big"), int.from_bytes(key_y, "big")
    value = ((rotate(x, 2) ^ y) + 0x1FF9E9AAC5FE0408024591DC5D52768A) & mask
    return rotate(value, 87).to_bytes(16, "big")
