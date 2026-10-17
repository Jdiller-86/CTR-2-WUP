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
        self.assertEqual(report['content_layout'], 'one_file_per_content')
        self.assertEqual(report['content_count'], len(report['files']) + 1)
        self.assertEqual(fst[:13], b'FST\0'+struct.pack('>II',32,report['content_count'])+b'\x01')
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
        count=struct.unpack_from('>I',data,8)[0]
        nodes=struct.unpack_from('>I',data,32+count*32+8)[0]
        strings=32+count*32+nodes*16
        data[strings+1:strings+5]=b'../x';path.write_bytes(data)
        path=self.output/'title.tmd';tmd=bytearray(path.read_bytes())
        tmd[CONTENT_START+16:CONTENT_START+48]=hashlib.sha1(data).digest()+bytes(12)
        tmd[0x208:0x228]=hashlib.sha256(tmd[CONTENT_START:]).digest()
        tmd[0x1e4:0x204]=hashlib.sha256(tmd[0x204:CONTENT_START]).digest()
        path.write_bytes(sign_document(tmd,self.signing[1],kind='tmd'))
        with self.assertRaises(HostError) as error:verify_package(self.output)
        self.assertEqual(error.exception.code,'invalid_fst')

    def test_installer_xml_boundary_uses_separate_zero_offset_streams(self):
        staged=self.root/'title'
        stage_title(self.source/'code/probe.rpx',staged,title_id='0005000043543257',
                    name='CTR2WUP Install Check',version=27)
        self.assertEqual((staged/'code/app.xml').stat().st_size,706)
        pack_homebrew(staged,self.output)
        # Decode independently of the writer and deep verifier. The old layout
        # placed both XML files in stream 1 (cos.xml at byte 736).
        fst=(self.output/'00000000.app').read_bytes()
        count=struct.unpack_from('>I',fst,8)[0]
        self.assertEqual(count,9)
        base=32+count*32
        nodes=struct.unpack_from('>I',fst,base+8)[0]
        strings=base+nodes*16
        seen=set()
        for n in range(1,nodes):
            kind,offset,size,perms,stream=struct.unpack_from('>IIIHH',fst,base+n*16)
            if kind>>24:
                continue
            start=strings+(kind&0xffffff)
            name=fst[start:fst.index(b'\0',start)].decode()
            self.assertEqual(offset,0,name)
            self.assertNotIn(stream,seen,name);seen.add(stream)
            self.assertEqual(perms,0x666)
            path=next(p for p in staged.rglob(name) if p.is_file())
            data=(self.output/f'{stream:08x}.app').read_bytes()
            self.assertEqual(data[:size],path.read_bytes(),name)
            self.assertFalse(any(data[size:]),name)
        self.assertEqual(seen,set(range(1,count)))
        ticket=(self.output/'title.tik').read_bytes()
        self.assertEqual(ticket[0x2d0:0x350],b'\xff'*128)

    def test_shared_plaintext_stream_and_missing_rights_are_rejected_after_resigning(self):
        pack_homebrew(self.source,self.output)
        path=self.output/'00000000.app';original=path.read_bytes();data=bytearray(original)
        count=struct.unpack_from('>I',data,8)[0];base=32+count*32
        # The second XML file aliases the first stream at offset zero. Repair
        # all hashes/signatures so rejection depends on the stream contract.
        struct.pack_into('>H',data,base+3*16+14,1)
        path.write_bytes(data)
        tmd_path=self.output/'title.tmd';tmd=bytearray(tmd_path.read_bytes())
        tmd[CONTENT_START+16:CONTENT_START+48]=hashlib.sha1(data).digest()+bytes(12)
        tmd[0x208:0x228]=hashlib.sha256(tmd[CONTENT_START:]).digest()
        tmd[0x1e4:0x204]=hashlib.sha256(tmd[0x204:CONTENT_START]).digest()
        tmd_path.write_bytes(sign_document(tmd,self.signing[1],kind='tmd'))
        with self.assertRaises(HostError) as error:verify_package(self.output)
        self.assertEqual(error.exception.code,'invalid_fst')
        path.write_bytes(original)
        tmd[CONTENT_START+16:CONTENT_START+48]=hashlib.sha1(original).digest()+bytes(12)
        tmd[0x208:0x228]=hashlib.sha256(tmd[CONTENT_START:]).digest()
        tmd[0x1e4:0x204]=hashlib.sha256(tmd[0x204:CONTENT_START]).digest()
        tmd_path.write_bytes(sign_document(tmd,self.signing[1],kind='tmd'))
        path=self.output/'title.tik';ticket=bytearray(path.read_bytes());ticket[-1]=0
        path.write_bytes(sign_document(ticket,self.signing[2],kind='ticket'))
        with self.assertRaises(HostError) as error:verify_package(self.output)
        self.assertEqual(error.exception.code,'invalid_ticket_rights')

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
