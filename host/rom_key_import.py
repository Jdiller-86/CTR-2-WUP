"""Import locally obtained plaintext GodMode9-format data; no console access."""
import json
from pathlib import Path
import shutil
import struct
import tempfile

from .atomic import publish_directory
from .rom_keys import RomKeys, fail, local_bytes

MAX_ENTRIES = 2048


def database(path, label):
    data = local_bytes(path, 16 + MAX_ENTRIES * 32)
    if len(data) < 16:
        fail("invalid_rom_key_database", label + " header is truncated")
    count = struct.unpack_from('<I', data)[0]
    if count > MAX_ENTRIES or len(data) != 16 + count * 32 or any(data[4:16]):
        fail("invalid_rom_key_database", label + " count, padding or length is invalid")
    return [data[16+i*32:48+i*32] for i in range(count)]


def import_keys(output, *, aeskeydb=None, seeddb=None, decrypted_titlekeys=None):
    """Values are exported ONLY to this explicitly selected private local store."""
    output = Path(output).absolute()
    if output.exists() or output.is_symlink(): fail("output_exists", "Key store already exists and was preserved")
    if not any((aeskeydb, seeddb, decrypted_titlekeys)):
        fail("missing_rom_key_database", "Choose at least one locally obtained key or seed file")
    maps = {role: {} for role in RomKeys.ROLES}
    def add(role, identity, value):
        if identity in maps[role]: fail("duplicate_rom_key", "Key/seed database has a duplicate supported identity")
        maps[role][identity] = value
    ignored = 0
    if aeskeydb:
        data = local_bytes(aeskeydb, MAX_ENTRIES * 32)
        if not data or len(data) % 32:
            fail("invalid_rom_key_database", "AES key database must contain whole 32-byte records")
        for start in range(0, len(data), 32):
            slot, kind = data[start:start+2]; identifier = data[start+2:start+12]
            unit, encrypted = data[start+14:start+16]
            if slot > 63 or kind not in b'XYNI' or any(data[start+12:start+14]) or unit > 2:
                fail("invalid_rom_key_database", "AES key database has an invalid record")
            if encrypted:
                fail("encrypted_key_database", "Import a decrypted AES key database; database-key decryption is not implemented")
            if unit != 1 and kind == ord('X') and slot in (0x2c, 0x25, 0x18, 0x1b) and not any(identifier):
                add('ncch_key_x_files', f'{slot:02x}', data[start+16:start+32])
            else: ignored += 1
    if seeddb:
        for record in database(seeddb, 'Seed database'):
            identity = f"{struct.unpack_from('<Q', record)[0]:016x}"
            if any(record[24:32]): fail("invalid_rom_key_database", "Seed record has invalid reserved bytes")
            if identity.startswith('00040000'): add('title_seed_files', identity, record[8:24])
            else: ignored += 1
    if decrypted_titlekeys:
        for record in database(decrypted_titlekeys, 'Decrypted title-key database'):
            index = struct.unpack_from('<I', record)[0]; identity = record[8:16].hex()
            if index > 5 or any(record[4:8]): fail("invalid_rom_key_database", "Title-key record is invalid")
            if identity.startswith('00040000'): add('cia_title_key_files', identity, record[16:32])
            else: ignored += 1
    if not any(maps.values()): fail("no_supported_rom_keys", "No supported retail base-application key/seed was found")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.ctr2wup-key-import-', dir=output.parent))
    try:
        document = {'schema_version': 1}
        for role, entries in maps.items():
            document[role] = {}
            for identity, value in entries.items():
                name = role + '-' + identity + '.bin'
                path = stage / name
                with path.open('xb') as handle: handle.write(value)
                path.chmod(0o600); document[role][identity] = name
        config = stage / 'keys.json'; config.write_text(json.dumps(document, indent=2)+'\n'); config.chmod(0o600)
        RomKeys(config)  # Validate the same schema the decryption engine consumes.
        report = {'ok': True, 'stage': 'import-rom-keys', 'schema_version': 1,
                  'imported_counts': {role: len(entries) for role, entries in maps.items()},
                  'ignored_records': ignored, 'local_private_store': True,
                  'device_extraction_performed': False, 'key_values_in_report': False,
                  'console_dump_verified': False,
                  'titlekey_plaintext_declared_by_user': decrypted_titlekeys is not None}
        (stage / 'key-import-report.json').write_text(json.dumps(report, indent=2)+'\n')
        publish_directory(stage, output)
        return {**report, 'output': str(output), 'config_filename': 'keys.json'}
    finally:
        if stage.exists(): shutil.rmtree(stage)
