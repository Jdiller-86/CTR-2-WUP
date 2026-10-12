"""Original bounded NCCH RomFS integrity and Level-3 metadata inspection."""
import hashlib
import struct

from .containers import check_overlaps, check_range
from .errors import HostError

NONE = 0xffffffff
MAX_TABLE_BYTES = 32 * 1024 * 1024
MAX_ENTRIES = 131072
MAX_BLOCKS = 2000000
MAX_PATH_CHARS = 4096
MAX_TOTAL_PATH_CHARS = 16 * 1024 * 1024


def fail(code, message):
    raise HostError(code, message, stage="assets")


def align(size, block):
    return (size + block - 1) // block * block


class Region:
    def __init__(self, reader, base, size):
        check_range(base, size, reader.size, "RomFS backing")
        self.reader, self.base, self.size = reader, base, size

    def read(self, offset, size):
        check_range(offset, size, self.size, "RomFS region")
        return self.reader.read(self.base + offset, size, "RomFS")

    def digest(self, offset, size):
        check_range(offset, size, self.size, "RomFS hashed region")
        return self.reader.digest(self.base + offset, size)


def verify_ivfc(region, *, anchored_bytes=None):
    """Verify every declared hash/data block in the standard three-level layout.

    NCCH's separately checked superblock must contain the complete master table
    to establish that anchor. This proves hash consistency, not authentic signing.
    Final physical blocks include zero padding; padding is checked explicitly.
    """
    header = region.read(0, 0x60)
    if header[:8] != b'IVFC\0\0\x01\0':
        fail("unsupported_ivfc", "Require the standard NCCH IVFC version 0x10000")
    master_size = struct.unpack_from('<I', header, 8)[0]
    levels = []
    for i in range(3):
        logical, size, exponent, reserved = struct.unpack_from('<QQII', header, 12 + 24*i)
        if reserved or not 5 <= exponent <= 20 or not size:
            fail("invalid_ivfc_header", "IVFC level size, block exponent or reserved bytes are invalid")
        levels.append({'logical_offset': logical, 'size': size, 'block': 1 << exponent})
    if master_size == 0 or master_size % 32:
        fail("invalid_ivfc_header", "IVFC master hashes must be a nonempty SHA-256 table")
    logical = 0
    for level in levels:
        if level['logical_offset'] != logical:
            fail("unsupported_ivfc_layout", "IVFC logical offsets do not use the canonical NCCH layout")
        logical += align(level['size'], level['block'])
        if logical >= 1 << 64:
            fail("invalid_ivfc_header", "IVFC logical extent overflows")
    if any(levels[i]['size'] % 32 for i in (0, 1)):
        fail("invalid_ivfc_header", "IVFC hash levels must contain whole SHA-256 entries")
    level3 = align(0x60 + master_size, levels[2]['block'])
    levels[2]['offset'] = level3
    levels[0]['offset'] = level3 + align(levels[2]['size'], levels[2]['block'])
    levels[1]['offset'] = levels[0]['offset'] + align(levels[0]['size'], levels[0]['block'])
    check_range(0x60, master_size, region.size, "IVFC master table")
    if anchored_bytes is not None and not 0x60 + master_size <= anchored_bytes <= region.size:
        fail("unanchored_ivfc", "NCCH superblock does not cover the complete IVFC master table")
    extents = [(0, 0x60 + master_size, 'IVFC header/master')]
    total_blocks = 0
    for level in levels:
        count = (level['size'] + level['block'] - 1) // level['block']
        total_blocks += count; level['blocks'] = count
        check_range(level['offset'], count * level['block'], region.size, "IVFC physical level")
        extents.append((level['offset'], count * level['block'], 'IVFC level'))
    if total_blocks > MAX_BLOCKS:
        fail("ivfc_limit", "IVFC block count exceeds the supported verification budget")
    check_overlaps(extents)
    level3_hash = hashlib.sha256()
    for i, level in enumerate(levels):
        parent_offset = 0x60 if i == 0 else levels[i-1]['offset']
        parent_size = master_size if i == 0 else levels[i-1]['size']
        if parent_size != level['blocks'] * 32:
            fail("invalid_ivfc_hash_count", "IVFC parent hash count differs from its child block count")
        for block in range(level['blocks']):
            data = region.read(level['offset'] + block * level['block'], level['block'])
            used = min(level['block'], level['size'] - block * level['block'])
            if any(data[used:]):
                fail("invalid_ivfc_padding", "IVFC final block contains nonzero padding")
            expected = region.read(parent_offset + block * 32, 32)
            if hashlib.sha256(data).digest() != expected:
                fail("ivfc_hash_mismatch", f"IVFC level {i+1} block {block} failed SHA-256 verification")
            if i == 2: level3_hash.update(data[:used])
    return {'version': 0x10000, 'levels': levels, 'master_hash_bytes': master_size,
            'verified_blocks': total_blocks, 'level3_offset': level3,
            'level3_size': levels[2]['size'], 'level3_sha256': level3_hash.hexdigest(),
            'all_declared_blocks_verified': True, 'ncch_superblock_anchored': anchored_bytes is not None,
            'signature_authenticity_verified': False}


def component(raw, *, root=False):
    if len(raw) % 2 or len(raw) > 512:
        fail("invalid_romfs_name", "RomFS name is too long or has an odd UTF-16 length")
    try:
        name = raw.decode('utf-16le')
    except UnicodeDecodeError:
        fail("invalid_romfs_name", "RomFS name contains invalid UTF-16")
    if root:
        if name: fail("invalid_romfs_root", "RomFS root directory has a name")
    elif not name or name in ('.', '..') or any(c in '/\\' or ord(c) < 32 for c in name):
        fail("invalid_romfs_name", "RomFS name is not a safe virtual path component")
    return name


def name_hash(parent, raw, buckets):
    value = parent ^ 123456789
    for (unit,) in struct.iter_unpack('<H', raw):
        value = (((value >> 5) | (value << 27)) & 0xffffffff) ^ unit
    return value % buckets


def records(data, *, directory):
    entries = {}; offset = 0; fixed = 24 if directory else 32
    while offset < len(data):
        if len(entries) >= MAX_ENTRIES or len(data) - offset < fixed:
            fail("invalid_romfs_table", "RomFS metadata is truncated or exceeds its entry budget")
        if directory:
            parent, sibling, child, first_file, next_hash, name_size = struct.unpack_from('<6I', data, offset)
            entry = {'parent': parent, 'sibling': sibling, 'child': child,
                     'first_file': first_file, 'next_hash': next_hash}
        else:
            parent, sibling, at, size, next_hash, name_size = struct.unpack_from('<IIQQII', data, offset)
            entry = {'parent': parent, 'sibling': sibling, 'data_offset': at,
                     'size': size, 'next_hash': next_hash}
        end = offset + align(fixed + name_size, 4)
        if end > len(data) or any(data[offset+fixed+name_size:end]):
            fail("invalid_romfs_table", "RomFS name extent or alignment padding is invalid")
        raw = data[offset+fixed:offset+fixed+name_size]
        entry.update(name=component(raw, root=directory and offset == 0), raw_name=raw)
        entries[offset] = entry; offset = end
    return entries


def hash_chains(table, entries):
    if not table or len(table) % 4:
        fail("invalid_romfs_hash_table", "RomFS hash buckets must be nonempty aligned words")
    seen = set(); buckets = len(table) // 4
    for bucket, (first,) in enumerate(struct.iter_unpack('<I', table)):
        cursor = first
        while cursor != NONE:
            if cursor not in entries or cursor in seen:
                fail("invalid_romfs_hash_chain", "RomFS hash chain contains an invalid pointer, duplicate or cycle")
            entry = entries[cursor]
            if name_hash(entry['parent'], entry['raw_name'], buckets) != bucket:
                fail("invalid_romfs_hash_chain", "RomFS metadata is in the wrong hash bucket")
            seen.add(cursor); cursor = entry['next_hash']
    if seen != entries.keys():
        fail("invalid_romfs_hash_chain", "RomFS hash table does not reach every metadata record")


def inspect_level3(region, *, hash_files=True):
    header = region.read(0, 40); values = struct.unpack('<10I', header)
    if values[0] != 40: fail("invalid_romfs_header", "Level-3 header length is not 40")
    areas = {}; extents = [(0, 40, 'Level-3 header')]
    for name, index in (('dir_hash', 1), ('dir_meta', 3), ('file_hash', 5), ('file_meta', 7)):
        offset, size = values[index:index+2]
        if offset < 40 or offset % 4 or size > MAX_TABLE_BYTES:
            fail("romfs_metadata_limit", "RomFS metadata offset/alignment/size is unsupported")
        check_range(offset, size, region.size, name)
        areas[name] = region.read(offset, size); extents.append((offset, size, name))
    data_offset = values[9]
    if data_offset < 40 or data_offset % 16:
        fail("invalid_romfs_header", "RomFS file-data offset must be 16-byte aligned")
    check_range(data_offset, region.size-data_offset, region.size, 'RomFS file data')
    extents.append((data_offset, region.size-data_offset, 'file data')); check_overlaps(extents)
    dirs = records(areas['dir_meta'], directory=True); files = records(areas['file_meta'], directory=False)
    if 0 not in dirs or dirs[0]['parent'] != 0 or dirs[0]['sibling'] != NONE:
        fail("invalid_romfs_root", "RomFS root parent/sibling is invalid")
    hash_chains(areas['dir_hash'], dirs); hash_chains(areas['file_hash'], files)
    pending = [(0, '', 0)]; seen_dirs = {0}; seen_files = set(); result = []; file_bytes = 0; path_chars = 0
    def virtual_path(parent, name):
        nonlocal path_chars
        length = len(parent) + 1 + len(name)
        if length > MAX_PATH_CHARS or path_chars + length > MAX_TOTAL_PATH_CHARS:
            fail("romfs_path_limit", "RomFS virtual paths exceed the supported manifest budget")
        path_chars += length
        return parent + '/' + name
    def children(first, entries, parent, names, seen):
        cursor = first
        while cursor != NONE:
            if cursor not in entries or cursor in seen:
                fail("invalid_romfs_tree", "RomFS child chain contains an invalid pointer, duplicate or cycle")
            entry = entries[cursor]
            if entry['parent'] != parent or entry['name'] in names:
                fail("invalid_romfs_tree", "RomFS parent or sibling name is inconsistent")
            names.add(entry['name']); seen.add(cursor)
            yield cursor, entry; cursor = entry['sibling']
    while pending:
        at, path, depth = pending.pop()
        if depth > 128: fail("romfs_depth_limit", "RomFS hierarchy exceeds 128 directories")
        entry = dirs[at]; names = set()
        for child, record in children(entry['child'], dirs, at, names, seen_dirs):
            pending.append((child, virtual_path(path, record['name']), depth+1))
        for _, record in children(entry['first_file'], files, at, names, seen_files):
            check_range(record['data_offset'], record['size'], region.size-data_offset, 'RomFS file payload')
            file_bytes += record['size']
            if file_bytes > 16 * 1024**3:
                fail("romfs_file_hash_limit", "Cumulative RomFS file bytes exceed the verification budget")
            offset = data_offset + record['data_offset']
            item = {'path': virtual_path(path, record['name']), 'offset': offset, 'size': record['size']}
            if hash_files: item['sha256'] = region.digest(offset, record['size'])
            result.append(item)
    if seen_dirs != dirs.keys() or seen_files != files.keys():
        fail("invalid_romfs_tree", "RomFS hierarchy does not reach every metadata record")
    return {'directories': len(dirs), 'files': sorted(result, key=lambda item: item['path']),
            'file_data_offset': data_offset, 'file_bytes': file_bytes,
            'guest_paths_only': True, 'host_paths_extracted': False}
