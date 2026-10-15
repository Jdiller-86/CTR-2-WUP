"""Original plaintext candidates; no console installation is inferred."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from host.errors import HostError
from host.package_homebrew import pack_homebrew
from host.package_integrity import verify_package
from host.package_validator import CONTENT_START, ValidatorError, validate_package
from host.package_test_signing import generate_chain, sign_document
from host.title_stage import stage_title
from tests.test_package_writer import original_inputs


@unittest.skipUnless(importlib.util.find_spec('cryptography'), 'install packaging dependency')
class PlainHomebrewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.signing = generate_chain()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source, config, key = original_inputs(self.root)
        config.unlink();key.unlink();(self.root/'synthetic.cert').unlink()
        self.output = self.root/'plain'
        p = patch('host.package_test_signing.generate_chain', return_value=self.signing)
        p.start();self.addCleanup(p.stop)

    def test_plaintext_roundtrip_needs_no_external_key_or_certificate(self):
        report = pack_homebrew(self.source, self.output)
        self.assertEqual(report['validation'], 'plaintext_integrity')
        self.assertFalse(report['common_key_required'])
        self.assertFalse(report['production_certificates_required'])
        self.assertFalse(report['console_verified'])
        self.assertTrue(report['requires_signature_patches'])
        self.assertTrue(all(not c['encrypted'] for c in report['contents']))
        self.assertFalse(report['test_signing']['root_trusted'])
        self.assertEqual(verify_package(self.output, expected_staging=self.source)['staging_comparison'],
                         'all files and directories match')
        fst=(self.output/'00000000.app').read_bytes()
        self.assertEqual(fst[:13], b'FST\0'+struct.pack('>II',32,4)+b'\x01')
        self.assertEqual(fst[32:64], bytes(32))

    def test_plaintext_corruption_and_invalid_signature_are_independently_rejected(self):
        pack_homebrew(self.source,self.output)
        path=self.output/'00000001.app';data=path.read_bytes();path.write_bytes(bytes([data[0]^1])+data[1:])
        with self.assertRaises(ValidatorError):validate_package(self.output)
        path.write_bytes(data)
        path=self.output/'title.tik';data=bytearray(path.read_bytes());data[4]^=1;path.write_bytes(data)
        with self.assertRaises(HostError) as error:verify_package(self.output)
        self.assertEqual(error.exception.code,'invalid_test_signature')

    def test_repaired_metadata_does_not_hide_a_bad_fst_path(self):
        pack_homebrew(self.source,self.output)
        path=self.output/'00000000.app';data=bytearray(path.read_bytes())
        nodes=struct.unpack_from('>I',data,32+4*32+8)[0]
        strings=32+4*32+nodes*16
        data[strings+1:strings+5]=b'../x';path.write_bytes(data)
        path=self.output/'title.tmd';tmd=bytearray(path.read_bytes())
        tmd[CONTENT_START+16:CONTENT_START+48]=hashlib.sha1(data).digest()+bytes(12)
        tmd[0x208:0x228]=hashlib.sha256(tmd[CONTENT_START:]).digest()
        tmd[0x1e4:0x204]=hashlib.sha256(tmd[0x204:CONTENT_START]).digest()
        path.write_bytes(sign_document(tmd,self.signing[1],kind='tmd'))
        with self.assertRaises(HostError) as error:verify_package(self.output)
        self.assertEqual(error.exception.code,'invalid_fst')

    def test_cancelled_or_existing_output_is_preserved(self):
        with patch('host.package_homebrew._encrypt_group',side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):pack_homebrew(self.source,self.output)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob('.ctr2wup-homebrew-*')))
        self.output.mkdir();(self.output/'keep').write_bytes(b'original')
        with self.assertRaises(HostError):pack_homebrew(self.source,self.output)
        self.assertEqual((self.output/'keep').read_bytes(),b'original')

    def test_staging_has_installed_filesystem_permissions_and_normal_cos_fields(self):
        staged=self.root/'title'
        stage_title(self.source/'code/probe.rpx',staged,title_id='0005000043543257',name='Original test')
        cos=ET.parse(staged/'code/cos.xml').getroot()
        self.assertIsNotNone(cos.find('cmdFlags'));self.assertIsNone(cos.find('cmd_flags'))
        groups={int(p.findtext('group')) for p in cos.find('permissions')}
        self.assertIn(11,groups)
        self.assertEqual(cos.find('argstr').get('length'),'4096')
        report=pack_homebrew(staged,self.output)
        self.assertTrue(report['ok'])
