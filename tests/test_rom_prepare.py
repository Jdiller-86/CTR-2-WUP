import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from host.cli import main
from host.containers import inspect
from host.errors import HostError
from host.intake_export import export_intake
from host.rom_keys import RomKeys, scramble
from host.rom_prepare import prepare_rom, cipher
from tests.host_fixtures import cia, ncch, ncsd, rehash_exefs

try:
    from tests.encrypted_fixtures import (COMMON_KEY, KEY_X, encrypted_cia, encrypted_ncch,
                                         reference_normal, write_key_config)
    CRYPTO = True
except ImportError:
    CRYPTO = False


class PlainPreparationTests(unittest.TestCase):
    def test_plain_cxi_needs_no_keys_or_crypto(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root / 'original.cxi'; source.write_bytes(ncch())
            with patch('host.rom_prepare.cipher', side_effect=AssertionError('crypto accessed')):
                result = prepare_rom(source, root / 'prepared')
            self.assertEqual((root / 'prepared/game.cxi').read_bytes(), source.read_bytes())
            self.assertFalse(result['ncch']['encrypted'])
            self.assertFalse(result['keys_exported'])

    def test_existing_output_and_symlink_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = root / 'original.cxi'; source.write_bytes(ncch())
            output = root / 'prepared'; output.mkdir(); (output / 'keep').write_text('keep')
            with self.assertRaises(HostError): prepare_rom(source, output)
            self.assertEqual((output / 'keep').read_text(), 'keep')
            link = root / 'linked'; link.symlink_to(output, target_is_directory=True)
            with self.assertRaises(HostError): prepare_rom(source, link)
            self.assertTrue(link.is_symlink())


@unittest.skipUnless(CRYPTO, 'optional decryption dependency is unavailable')
class EncryptedPreparationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = Path(self.tmp.name)
        self.config = write_key_config(self.root / 'private key inputs 日本語')
        self.source = self.root / 'original input.cia'; self.output = self.root / 'prepared output 日本語'

    def tearDown(self): self.tmp.cleanup()

    def run_case(self, data, expected=None):
        self.source.write_bytes(data)
        result = prepare_rom(self.source, self.output, keys_config=self.config)
        self.assertEqual(self.source.read_bytes(), data)
        if expected is not None: self.assertEqual((self.output / 'game.cxi').read_bytes(), expected)
        self.assertEqual(set(p.name for p in self.output.iterdir()), {'game.cxi', 'preparation-report.json'})
        text = (self.output / 'preparation-report.json').read_text()
        for value in (*KEY_X.values(), COMMON_KEY): self.assertNotIn(value.hex(), text)
        self.assertNotIn(str(self.config), text)
        self.assertFalse(result['commercial_profile_enabled'])
        return result

    def reject(self, data, code):
        self.source.write_bytes(data)
        with self.assertRaises(HostError) as caught: prepare_rom(self.source, self.output, keys_config=self.config)
        self.assertEqual(caught.exception.code, code)
        self.assertEqual(caught.exception.stage, 'prepare-rom')
        self.assertEqual(self.source.read_bytes(), data)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob('.ctr2wup-prepare-*')))

    def test_nist_aes_cbc_known_vector(self):
        key = bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c')
        iv = bytes.fromhex('000102030405060708090a0b0c0d0e0f')
        ciphertext = bytes.fromhex('7649abac8119b246cee98e9b12e9197d')
        decrypt = cipher(key, iv).decryptor()
        self.assertEqual(decrypt.update(ciphertext) + decrypt.finalize(),
                         bytes.fromhex('6bc1bee22e409f96e93d7e117393172a'))

    def test_nist_aes_ctr_known_vector(self):
        key = bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c')
        iv = bytes.fromhex('f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff')
        decrypt = cipher(key, iv, ctr=True).decryptor()
        self.assertEqual(decrypt.update(bytes.fromhex('874d6191b620e3261bef6864990db6ce')) + decrypt.finalize(),
                         bytes.fromhex('6bc1bee22e409f96e93d7e117393172a'))

    def test_key_scrambler_independent_bit_and_carry_reference(self):
        for x in (bytes(16), bytes([255]) * 16, bytes(range(16)), bytes(range(240, 256))):
            for y in (bytes(16), bytes([255]) * 16, bytes(range(16))):
                self.assertEqual(scramble(x, y), reference_normal(x, y))

    def test_all_supported_ncch_methods_and_seed_routes(self):
        for method in (0, 1, 10, 11):
            for seed in (False, True):
                with self.subTest(method=method, seed=seed):
                    encrypted, plain = encrypted_ncch(method=method, seeded=seed)
                    self.output = self.root / ('prepared-' + str(method) + '-' + str(seed))
                    result = self.run_case(encrypted, plain)
                    self.assertEqual(result['ncch']['seed_used'], seed)
                    self.assertTrue(result['romfs_preserved'])
                    self.assertEqual(result['executable_revision'], inspect(self.output / 'game.cxi')['executable_revision'])

    def test_double_encrypted_cia_to_private_executable_intake(self):
        encrypted, plain = encrypted_ncch(method=1)
        result = self.run_case(encrypted_cia(encrypted, auxiliary=True), plain)
        self.assertTrue(result['outer_cia_decrypted'])
        self.assertEqual(result['cia_key_route'], 'local_common_key_and_ticket')
        intake = export_intake(self.output / 'game.cxi', self.root / 'small intake')
        self.assertEqual(intake['executable_revision'], result['executable_revision'])
        self.assertTrue(intake['executable_available'])

    def test_outer_only_and_plain_cia(self):
        plain = bytes(ncch())
        result = self.run_case(encrypted_cia(plain), plain)
        self.assertFalse(result['ncch']['encrypted'])
        self.output = self.root / 'plain-cia'; result = self.run_case(cia([(0, plain)]), plain)
        self.assertFalse(result['outer_cia_decrypted'])

    def test_encrypted_ncsd_extracts_main(self):
        encrypted, plain = encrypted_ncch(method=11)
        result = self.run_case(ncsd([(0, encrypted), (1, ncch())]), plain)
        self.assertEqual(result['input_format'], 'NCSD')
        self.assertFalse(result['other_contents_exported'])

    def test_streamed_cbc_and_ctr_cross_chunk_boundaries(self):
        from host.rom_prepare import CHUNK
        payload = bytes(range(256)) * ((CHUNK + 512) // 256)
        encrypted, plain = encrypted_ncch(method=10, romfs_payload=payload)
        self.run_case(encrypted_cia(encrypted), plain)

    def test_compressed_code_is_checked_after_decryption(self):
        encrypted, plain = encrypted_ncch(compressed=True)
        self.run_case(encrypted, plain)
        self.assertTrue(inspect(self.output / 'game.cxi')['partitions'][0]['code_compressed'])

    def test_personalized_ticket_requires_explicit_recovered_title_key(self):
        encrypted, plain = encrypted_ncch()
        data = encrypted_cia(encrypted, personalized=True)
        self.reject(data, 'personalized_ticket')
        self.config = write_key_config(self.root / 'direct inputs', direct_title_key=True)
        self.assertEqual(self.run_case(data, plain)['cia_key_route'], 'local_title_key')

    def test_wrong_cia_key_and_damaged_ciphertext_preserve_source(self):
        encrypted, _ = encrypted_ncch(); data = encrypted_cia(encrypted)
        (self.config.parent / 'synthetic-common.bin').write_bytes(bytes(16))
        self.reject(data, 'cia_content_hash_mismatch')
        (self.config.parent / 'synthetic-common.bin').write_bytes(COMMON_KEY)
        data[-1] ^= 1; self.reject(data, 'cia_content_hash_mismatch')

    def test_wrong_primary_and_secondary_keys_are_rejected(self):
        encrypted, _ = encrypted_ncch(method=1)
        (self.config.parent / 'synthetic-2c.bin').write_bytes(bytes(16))
        self.reject(encrypted, 'hash_mismatch')
        (self.config.parent / 'synthetic-2c.bin').write_bytes(KEY_X['2c'])
        (self.config.parent / 'synthetic-25.bin').write_bytes(bytes(16))
        self.reject(encrypted, 'hash_mismatch')

    def test_wrong_or_missing_seed_is_rejected(self):
        encrypted, _ = encrypted_ncch(seeded=True)
        (self.config.parent / 'synthetic-seed.bin').write_bytes(bytes(16))
        self.reject(encrypted, 'invalid_title_seed')
        document = json.loads(self.config.read_text()); document['title_seed_files'] = {}
        self.config.write_text(json.dumps(document)); self.reject(encrypted, 'missing_rom_key')

    def test_missing_key_config_and_required_slots(self):
        encrypted, _ = encrypted_ncch(method=10); self.source.write_bytes(encrypted)
        with self.assertRaises(HostError) as caught: prepare_rom(self.source, self.output)
        self.assertEqual(caught.exception.code, 'missing_rom_keys')
        document = json.loads(self.config.read_text()); document['ncch_key_x_files'].pop('18')
        self.config.write_text(json.dumps(document)); self.reject(encrypted, 'missing_rom_key')

    def test_unsupported_crypto_and_malformed_section_bounds(self):
        encrypted, _ = encrypted_ncch(); data = bytearray(encrypted); data[0x18B] = 3
        self.reject(data, 'unsupported_ncch_crypto')
        data[0x18B] = 0; data[0x18F] |= 1; self.reject(data, 'unsupported_fixed_key')
        data[0x18F] &= ~1; struct.pack_into('<I', data, 0x1A0, 1); self.reject(data, 'overlapping_regions')

    def test_staging_race_cannot_replace_an_existing_destination(self):
        encrypted, _ = encrypted_ncch(); self.source.write_bytes(encrypted)
        from host.atomic import publish_directory
        def race(stage, output):
            output.mkdir(); (output / 'keep').write_text('keep'); publish_directory(stage, output)
        with patch('host.rom_prepare.publish_directory', race), self.assertRaises(HostError):
            prepare_rom(self.source, self.output, keys_config=self.config)
        self.assertEqual((self.output / 'keep').read_text(), 'keep')

    def test_cli_does_not_emit_keys_or_key_configuration_paths(self):
        encrypted, _ = encrypted_ncch(); self.source.write_bytes(encrypted)
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = main(['prepare-rom', str(self.source), '--keys-config', str(self.config), '--output', str(self.output)])
        self.assertEqual(code, 0); self.assertFalse(stderr.getvalue())
        result = json.loads(stdout.getvalue()); self.assertFalse(result['keys_exported'])
        self.assertNotIn(str(self.config), stdout.getvalue())
        self.assertNotIn(KEY_X['2c'].hex(), stdout.getvalue())

    def test_key_files_and_configuration_are_bounded_and_redacted(self):
        self.assertNotIn(str(self.config), repr(RomKeys(self.config)))
        self.config.write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaises(HostError): RomKeys(self.config)
        self.config.write_text('{"schema_version":1,"ncch_key_x_files":{"2c":"secret.bin"}}')
        (self.config.parent / 'secret.bin').write_bytes(bytes(17))
        with self.assertRaises(HostError) as caught: RomKeys(self.config).read('ncch_key_x_files', '2c')
        self.assertNotIn('secret.bin', str(caught.exception))
        (self.config.parent / 'secret.bin').unlink(); (self.config.parent / 'secret.bin').symlink_to(self.source)
        with self.assertRaises(HostError): RomKeys(self.config).read('ncch_key_x_files', '2c')


if __name__ == '__main__': unittest.main()
