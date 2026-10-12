import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from host.assets import prepare_assets
from host.containers import Reader, inspect
from host.errors import HostError
from host.romfs import Region, inspect_level3, verify_ivfc
from tests.host_fixtures import cia, ncch, ncsd
from tests.romfs_fixtures import ivfc_image, level3_image, ncch_romfs


class RomfsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.source = self.root/'original.cxi'; self.output = self.root/'private assets 日本語'

    def region(self, data):
        path = self.root/'fixture.bin'; path.write_bytes(data)
        self.handle = path.open('rb'); self.addCleanup(self.handle.close)
        return Region(Reader(self.handle), 0, len(data))

    def reject_ivfc(self, data, code):
        with self.assertRaises(HostError) as caught: verify_ivfc(self.region(data))
        self.assertEqual(caught.exception.code, code)

    def test_full_tree_and_canonical_physical_layout(self):
        original = level3_image(); data = ivfc_image(original)
        result = verify_ivfc(self.region(data), anchored_bytes=512)
        self.assertEqual([l['offset'] for l in result['levels']], [8192,12288,4096])
        self.assertEqual(result['verified_blocks'], 3)
        self.assertEqual(result['level3_sha256'], hashlib.sha256(original).hexdigest())
        self.assertTrue(result['ncch_superblock_anchored'])

    def test_multiple_blocks_different_block_sizes_and_final_zero_padding(self):
        for exponent in (5, 8, 12):
            with self.subTest(exponent=exponent):
                original = level3_image()+bytes(range(256))*100
                result = verify_ivfc(self.region(ivfc_image(original, exponent)))
                self.assertGreater(result['levels'][2]['blocks'], 1)
                self.assertEqual(result['level3_sha256'], hashlib.sha256(original).hexdigest())
                self.assertFalse(result['ncch_superblock_anchored'])

    def test_corruption_of_master_each_hash_level_and_data_is_rejected(self):
        raw = ivfc_image(level3_image()); good = verify_ivfc(self.region(raw))
        for offset in (0x60,good['levels'][0]['offset'],good['levels'][1]['offset'],good['levels'][2]['offset']):
            with self.subTest(offset=offset):
                data = bytearray(raw); data[offset] ^= 1
                self.reject_ivfc(data,'ivfc_hash_mismatch')

    def test_padding_corruption_is_rejected(self):
        data = bytearray(ivfc_image(level3_image()));data[4096+len(level3_image())] = 1
        self.reject_ivfc(data,'invalid_ivfc_padding')

    def test_invalid_version_block_exponents_extents_and_hash_counts(self):
        raw = ivfc_image(level3_image())
        for offset,value,code in ((4,2,'unsupported_ivfc'),(28,31,'invalid_ivfc_header'),
                                  (8,0,'invalid_ivfc_header'),(8,31,'invalid_ivfc_header'),
                                  (8,64,'invalid_ivfc_hash_count')):
            data=bytearray(raw);struct.pack_into('<I',data,offset,value);self.reject_ivfc(data,code)
        data=bytearray(raw);struct.pack_into('<Q',data,12,1);self.reject_ivfc(data,'unsupported_ivfc_layout')
        self.reject_ivfc(raw[:-1],'invalid_bounds')

    def test_master_table_must_be_covered_by_the_ncch_anchor(self):
        with self.assertRaises(HostError) as caught: verify_ivfc(self.region(ivfc_image(level3_image())),anchored_bytes=0x60)
        self.assertEqual(caught.exception.code,'unanchored_ivfc')

    def test_level3_metadata_paths_bytes_and_unicode(self):
        data=level3_image();result=inspect_level3(self.region(data))
        self.assertEqual(result['directories'],2)
        self.assertEqual([f['path'] for f in result['files']],['/levels/config.bin','/levels/設定.txt','/readme.txt'])
        for file in result['files']:
            self.assertEqual(file['sha256'],hashlib.sha256(data[file['offset']:file['offset']+file['size']]).hexdigest())
        self.assertFalse(result['host_paths_extracted'])
        for limit in ('MAX_PATH_CHARS','MAX_TOTAL_PATH_CHARS'):
            with patch('host.romfs.'+limit,6),self.assertRaises(HostError) as caught:
                inspect_level3(self.region(data))
            self.assertEqual(caught.exception.code,'romfs_path_limit')

    def test_out_of_bounds_metadata_and_file_data_are_rejected(self):
        data=bytearray(level3_image());struct.pack_into('<I',data,12,len(data)+4)
        with self.assertRaises(HostError):inspect_level3(self.region(data))
        data=bytearray(level3_image());struct.pack_into('<Q',data,124+8,2**63)
        with self.assertRaises(HostError):inspect_level3(self.region(data))

    def test_sibling_and_hash_cycles_or_orphans_are_rejected(self):
        for offset,value in ((52+24+4,24),(52+24+16,24),(52+8,0xffffffff),(40,0xffffffff)):
            data=bytearray(level3_image());struct.pack_into('<I',data,offset,value)
            with self.assertRaises(HostError):inspect_level3(self.region(data))

    def test_traversal_separator_null_and_invalid_utf16_names_are_rejected(self):
        for name in ('../bad','a/bad!','a\\bad!','\0bad!!'):
            data=bytearray(level3_image());encoded=name.encode('utf-16le');self.assertEqual(len(encoded),12)
            data[124+104+32:124+104+44]=encoded
            with self.assertRaises(HostError) as caught:inspect_level3(self.region(data))
            self.assertEqual(caught.exception.code,'invalid_romfs_name')
        data=bytearray(level3_image());data[124+104+32:124+104+34]=b'\0\xd8'
        with self.assertRaises(HostError):inspect_level3(self.region(data))

    def test_asset_blob_and_manifest_preserve_the_source_and_revision(self):
        data=ncch_romfs();self.source.write_bytes(data);before=inspect(self.source)
        result=prepare_assets(self.source,self.output)
        self.assertEqual((self.output/'romfs.bin').read_bytes(),level3_image())
        self.assertEqual(result['executable_revision'],before['executable_revision'])
        self.assertEqual(self.source.read_bytes(),data)
        self.assertEqual({p.name for p in self.output.iterdir()},{'romfs.bin','asset-manifest.json'})
        self.assertTrue(result['ivfc']['all_declared_blocks_verified'])
        self.assertEqual(json.loads((self.output/'asset-manifest.json').read_text())['metadata'],result['metadata'])

    def test_asset_preparation_supports_main_decrypted_cia_and_ncsd(self):
        for kind,data in (('CIA',cia([(0,ncch_romfs())])),('NCSD',ncsd([(0,ncch_romfs())]))):
            self.source.write_bytes(data);self.output=self.root/kind
            result=prepare_assets(self.source,self.output)
            self.assertEqual((self.output/'romfs.bin').read_bytes(),level3_image());self.assertTrue(result['ok'])

    def test_data_corruption_outside_the_old_superblock_check_is_caught(self):
        data=ncch_romfs();base=struct.unpack_from('<I',data,0x1b0)[0]*512
        data[base+4096+300]^=1;self.source.write_bytes(data)
        inspect(self.source) # This region is outside the old NCCH superblock hash.
        with self.assertRaises(HostError) as caught:prepare_assets(self.source,self.output)
        self.assertEqual(caught.exception.code,'ivfc_hash_mismatch')
        self.assertFalse(self.output.exists());self.assertFalse(list(self.root.glob('.ctr2wup-assets-*')))

    def test_missing_romfs_and_existing_output_fail_without_replacement(self):
        self.source.write_bytes(ncch())
        with self.assertRaises(HostError) as caught:prepare_assets(self.source,self.output)
        self.assertEqual(caught.exception.code,'missing_romfs')
        self.output.mkdir();(self.output/'keep').write_text('keep')
        with self.assertRaises(HostError):prepare_assets(self.source,self.output)
        self.assertEqual((self.output/'keep').read_text(),'keep')

    def test_publication_race_preserves_the_new_destination(self):
        self.source.write_bytes(ncch_romfs())
        from host.atomic import publish_directory
        def race(stage,output):
            output.mkdir();(output/'keep').write_text('keep');publish_directory(stage,output)
        with patch('host.assets.publish_directory',race),self.assertRaises(HostError):prepare_assets(self.source,self.output)
        self.assertEqual((self.output/'keep').read_text(),'keep')

    def test_local_source_change_during_copy_blocks_publication(self):
        self.source.write_bytes(ncch_romfs())
        from host.assets import verify_main_romfs
        def change(reader,main):
            result=verify_main_romfs(reader,main)
            with self.source.open('r+b') as handle:
                handle.seek(result[2].base+300);old=handle.read(1);handle.seek(result[2].base+300);handle.write(bytes([old[0]^1]))
            return result
        with patch('host.assets.verify_main_romfs',change),self.assertRaises(HostError) as caught:prepare_assets(self.source,self.output)
        self.assertEqual(caught.exception.code,'input_changed');self.assertFalse(self.output.exists())


if __name__=='__main__':unittest.main()
