import json
from pathlib import Path
import struct
import tempfile
import unittest

from host.errors import HostError
from host.rom_key_import import import_keys
from host.rom_keys import RomKeys


def aes_record(slot=0x2c, *, encrypted=0, unit=2):
    return bytes([slot, ord('X')]) + bytes(12) + bytes([unit, encrypted]) + bytes(range(16))


def seed_record(title=0x000400000ff00f00):
    return struct.pack('<Q', title) + bytes(range(224, 240)) + bytes(8)


def title_record(title=0x000400000ff00f00):
    return bytes(8) + title.to_bytes(8, 'big') + bytes(range(192, 208))


def database(*records): return struct.pack('<I', len(records)) + bytes(12) + b''.join(records)


class KeyImportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = Path(self.tmp.name)
        self.aes = self.root / 'aeskeydb.bin'; self.seeds = self.root / 'seeddb.bin'
        self.titles = self.root / 'decTitleKeys.bin'; self.output = self.root / 'private store 日本語'
        self.aes.write_bytes(aes_record()); self.seeds.write_bytes(database(seed_record()))
        self.titles.write_bytes(database(title_record()))

    def tearDown(self): self.tmp.cleanup()

    def test_plaintext_exports_import_with_redacted_report(self):
        report = import_keys(self.output, aeskeydb=self.aes, seeddb=self.seeds, decrypted_titlekeys=self.titles)
        keys = RomKeys(self.output / 'keys.json')
        self.assertEqual(keys.read('ncch_key_x_files', '2c'), bytes(range(16)))
        self.assertEqual(keys.read('title_seed_files', '000400000ff00f00'), bytes(range(224, 240)))
        self.assertEqual(keys.read('cia_title_key_files', '000400000ff00f00'), bytes(range(192, 208)))
        self.assertFalse(report['device_extraction_performed'])
        self.assertFalse(report['key_values_in_report'])
        self.assertNotIn(bytes(range(16)).hex(), json.dumps(report))
        self.assertNotIn(str(self.aes), json.dumps(report))

    def test_encrypted_key_db_is_rejected_without_store(self):
        self.aes.write_bytes(aes_record(encrypted=1))
        with self.assertRaises(HostError) as caught: import_keys(self.output, aeskeydb=self.aes)
        self.assertEqual(caught.exception.code, 'encrypted_key_database'); self.assertFalse(self.output.exists())

    def test_duplicate_supported_key_seed_or_title_is_rejected(self):
        for role, path, data in (('aeskeydb', self.aes, aes_record() * 2),
                                 ('seeddb', self.seeds, database(seed_record(), seed_record())),
                                 ('decrypted_titlekeys', self.titles, database(title_record(), title_record()))):
            with self.subTest(role=role):
                path.write_bytes(data)
                with self.assertRaises(HostError): import_keys(self.output, **{role: path})
                self.assertFalse(self.output.exists())

    def test_devkit_and_unrelated_records_are_not_used(self):
        self.aes.write_bytes(aes_record(unit=1) + aes_record(slot=0x11) + aes_record())
        report = import_keys(self.output, aeskeydb=self.aes)
        self.assertEqual(report['ignored_records'], 2)
        self.assertEqual(report['imported_counts']['ncch_key_x_files'], 1)

    def test_invalid_database_count_padding_and_size(self):
        for data in (b'', bytes(15), database(seed_record()) + b'X', struct.pack('<I', 2049) + bytes(12),
                     struct.pack('<I', 1) + b'X' + bytes(11) + seed_record()):
            self.seeds.write_bytes(data)
            with self.assertRaises(HostError): import_keys(self.output, seeddb=self.seeds)
            self.assertFalse(self.output.exists())

    def test_invalid_key_record_fields(self):
        for at, value in ((0, 64), (1, ord('Z')), (12, 1), (14, 3)):
            data = bytearray(aes_record()); data[at] = value; self.aes.write_bytes(data)
            with self.assertRaises(HostError): import_keys(self.output, aeskeydb=self.aes)
            self.assertFalse(self.output.exists())

    def test_existing_store_is_never_overwritten(self):
        self.output.mkdir(); (self.output / 'keep').write_text('keep')
        with self.assertRaises(HostError): import_keys(self.output, aeskeydb=self.aes)
        self.assertEqual((self.output / 'keep').read_text(), 'keep')

    def test_no_supported_records_and_missing_inputs_fail(self):
        self.aes.write_bytes(aes_record(unit=1))
        with self.assertRaises(HostError): import_keys(self.output, aeskeydb=self.aes)
        with self.assertRaises(HostError): import_keys(self.output)
        self.assertFalse(self.output.exists())

    def test_imported_exports_feed_double_encrypted_preparation(self):
        try:
            from tests.encrypted_fixtures import KEY_X, encrypted_cia, encrypted_ncch
        except ImportError: self.skipTest('optional crypto dependency is unavailable')
        raw = bytearray()
        for slot, value in KEY_X.items():
            raw += bytes([int(slot, 16), ord('X')]) + bytes(12) + bytes([2, 0]) + value
        self.aes.write_bytes(raw)
        import_keys(self.output, aeskeydb=self.aes, seeddb=self.seeds, decrypted_titlekeys=self.titles)
        encrypted, plain = encrypted_ncch(method=11, seeded=True)
        source = self.root / 'original.cia'; source.write_bytes(encrypted_cia(encrypted))
        from host.rom_prepare import prepare_rom
        result = prepare_rom(source, self.root / 'prepared', keys_config=self.output / 'keys.json')
        self.assertEqual((self.root / 'prepared/game.cxi').read_bytes(), plain)
        self.assertEqual(result['cia_key_route'], 'local_title_key')


if __name__ == '__main__': unittest.main()
