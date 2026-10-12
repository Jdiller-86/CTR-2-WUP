"""Original encrypted containers and deliberately synthetic keys for checks."""
import hashlib
from pathlib import Path
import struct

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

from tests.host_fixtures import align, cia, ncch, smdh

KEY_X = {slot: bytes((n + offset) % 256 for n in range(16))
         for slot, offset in (("2c", 16), ("25", 48), ("18", 80), ("1b", 112))}
COMMON_KEY = bytes(range(160, 176))
TITLE_KEY = bytes(range(192, 208))
SEED = bytes(range(224, 240))


def reference_normal(x, y):
    """Separate bit-string rotation and bytewise carry implementation."""
    def rotate(data, count):
        bits = ''.join(f'{byte:08b}' for byte in data)
        bits = bits[count:] + bits[:count]
        return bytes(int(bits[i:i+8], 2) for i in range(0, 128, 8))
    rotated = rotate(x, 2)
    mixed = bytes(a ^ b for a, b in zip(rotated, y))
    constant = bytes.fromhex('1ff9e9aac5fe0408024591dc5d52768a')
    result = bytearray(16); carry = 0
    for i in range(15, -1, -1):
        value = mixed[i] + constant[i] + carry
        result[i] = value & 255; carry = value >> 8
    return rotate(result, 87)


def transform(data, key, iv, mode):
    encrypt = Cipher(algorithms.AES(key), mode(iv)).encryptor()
    return encrypt.update(data) + encrypt.finalize()


def encrypted_ncch(*, method=0, seeded=False, compressed=False, romfs=True, romfs_payload=None, base_plain=None):
    from tests.host_fixtures import CODE, blz_repeat
    plain = bytearray(base_plain) if base_plain is not None else ncch([('banner', b'original banner'), ('.code', blz_repeat() if compressed else CODE),
                  ('icon', smdh()), ('extra', b'original asset bytes')], compressed=compressed)
    plain[:16] = bytes(range(16))
    if romfs:
        from tests.romfs_fixtures import level3_image, ivfc_image
        payload = ivfc_image(level3_image() + (romfs_payload or b''))
        at = len(plain); plain += payload; plain[0x18F] &= ~2
        struct.pack_into('<I', plain, 0x104, len(plain) // 512)
        struct.pack_into('<III', plain, 0x1B0, at // 512, len(payload) // 512, 1)
        plain[0x1E0:0x200] = hashlib.sha256(payload[:512]).digest()
    encoded = bytearray(plain); encoded[0x18F] &= ~4; encoded[0x18B] = method
    key_y = bytes(plain[:16]); primary = reference_normal(KEY_X['2c'], key_y)
    if seeded:
        encoded[0x18F] |= 0x20
        encoded[0x114:0x118] = hashlib.sha256(SEED + plain[0x118:0x120]).digest()[:4]
        plain[0x114:0x118] = encoded[0x114:0x118]
        key_y = hashlib.sha256(key_y + SEED).digest()[:16]
    secondary = reference_normal(KEY_X[{0: '2c', 1: '25', 10: '18', 11: '1b'}[method]], key_y)
    counter = bytes(plain[0x108:0x110][::-1])
    def encrypt_slice(start, length, key, section, relative=0):
        iv = counter + bytes([section]) + bytes(7)
        # Use the full section keystream to independently select an unaligned range.
        ciphertext = transform(bytes(relative) + plain[start:start+length], key, iv, modes.CTR)
        encoded[start:start+length] = ciphertext[relative:]
    encrypt_slice(512, 0x800, primary, 1)
    exefs = struct.unpack_from('<I', plain, 0x1A0)[0] * 512
    encrypt_slice(exefs, 512, primary, 2)
    for index in range(10):
        name, offset, length = struct.unpack_from('<8sII', plain, exefs + index * 16)
        if not length: continue
        start = exefs + 512 + offset
        encrypt_slice(start, length, primary if name.split(b'\0', 1)[0] in (b'icon', b'banner') else secondary,
                      2, 512 + offset)
        padding = align(length) - length
        encrypt_slice(start + length, padding, primary, 2, 512 + offset + length)
    if romfs: encrypt_slice(at, len(payload), secondary, 3)
    return bytes(encoded), bytes(plain)


def encrypted_cia(content, *, common_index=0, personalized=False, auxiliary=False):
    raw = cia([(0, content), (8, b'original auxiliary')] if auxiliary else [(0, content)],
              encrypted=True, title_id=int.from_bytes(content[0x118:0x120], 'little'))
    # Parse the original fixture's public container offsets, not production code.
    cert_length, ticket_length, tmd_length = struct.unpack_from('<III', raw, 8)
    ticket_at = align(0x2040 + cert_length, 64)
    tmd_at = align(ticket_at + ticket_length, 64)
    content_at = align(tmd_at + tmd_length, 64)
    raw[ticket_at + 0x1BF:ticket_at + 0x1CF] = transform(TITLE_KEY, COMMON_KEY,
        bytes(raw[ticket_at + 0x1DC:ticket_at + 0x1E4]) + bytes(8), modes.CBC)
    raw[ticket_at + 0x1F1] = common_index
    if personalized: raw[ticket_at + 0x1D8:ticket_at + 0x1DC] = b'USER'
    raw[content_at:content_at + len(content)] = transform(content, TITLE_KEY, bytes(16), modes.CBC)
    return raw


def write_key_config(directory, *, direct_title_key=False):
    import json
    directory = Path(directory); directory.mkdir(parents=True, exist_ok=True)
    references = {'schema_version': 1, 'ncch_key_x_files': {}, 'cia_common_key_files': {},
                  'cia_title_key_files': {}, 'title_seed_files': {}}
    for slot, data in KEY_X.items():
        name = 'synthetic-' + slot + '.bin'; (directory / name).write_bytes(data)
        references['ncch_key_x_files'][slot] = name
    (directory / 'synthetic-common.bin').write_bytes(COMMON_KEY)
    references['cia_common_key_files']['0'] = 'synthetic-common.bin'
    (directory / 'synthetic-seed.bin').write_bytes(SEED)
    references['title_seed_files']['000400000ff00f00'] = 'synthetic-seed.bin'
    if direct_title_key:
        (directory / 'synthetic-title.bin').write_bytes(TITLE_KEY)
        references['cia_title_key_files']['000400000ff00f00'] = 'synthetic-title.bin'
    path = directory / 'keys.json'; path.write_text(json.dumps(references))
    return path
