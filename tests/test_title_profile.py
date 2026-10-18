"""Menu compatibility checks must catch defects that a hash roundtrip cannot."""
import copy
from pathlib import Path
import tempfile
import struct
import unittest
import xml.etree.ElementTree as ET
import zlib

from host.errors import HostError
from host.title_stage import stage_title
from host.title_profile import verify_menu_profile
from host.rpx_startup import rpx_sdk
from tests.test_package_writer import original_inputs


class MenuProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            source,_,_=original_inputs(root)
            stage_title(source/'code/probe.rpx',root/'title',title_id='0005000043543257',
                        name='CTR2WUP Install Check',version=28)
            cls.files={p.relative_to(root/'title').as_posix():p.read_bytes()
                       for p in (root/'title').rglob('*') if p.is_file()}

    def test_full_original_profile_covers_acp_fields_and_tga2(self):
        self.assertEqual(verify_menu_profile(self.files),'wup-menu-v2')
        self.assertEqual(verify_menu_profile(self.files,require_startup=True),'wup-menu-v3')
        self.assertEqual(ET.fromstring(self.files['code/app.xml']).findtext('sdk_version'),'21301')
        self.assertEqual(len(ET.fromstring(self.files['meta/meta.xml'])),137)

    def test_new_profile_rejects_zero_or_other_sdk_without_rewriting_historical_profile(self):
        for sdk in ('0','20811','21302'):
            app=ET.fromstring(self.files['code/app.xml']);app.find('sdk_version').text=sdk
            files={**self.files,'code/app.xml':ET.tostring(app)}
            self.assertEqual(verify_menu_profile(files),'wup-menu-v2')
            with self.subTest(sdk=sdk),self.assertRaises(HostError) as caught:
                verify_menu_profile(files,require_startup=True)
            self.assertEqual(caught.exception.code,'invalid_native_startup')

    def test_sdk_read_requires_unique_bounded_uncompressed_record_and_its_crc(self):
        original=self.files['code/ctr2wup.rpx']
        cases=[original[:-1]]
        for offset,value in ((96,0),(56,0x80000004),(108,0xfffffff0),
                             (100,0x08000000),(112,95),(272,1),(252,0)):
            changed=bytearray(original);struct.pack_into('>I',changed,offset,value);cases.append(changed)
        for changed in cases:
            with self.subTest(bytes=len(changed)),self.assertRaises(HostError):rpx_sdk(changed)
        # A repaired CRC must not make a zero/unsupported SDK record acceptable.
        for offset,value in ((172,0xcafe0401),(224,0),(236,0),(252,0),(252,0x5000)):
            changed=bytearray(original);struct.pack_into('>I',changed,offset,value)
            struct.pack_into('>I',changed,272,zlib.crc32(changed[172:268]))
            with self.subTest(offset=offset,value=value),self.assertRaises(HostError):rpx_sdk(changed)

    def test_staging_preserves_output_when_rpx_has_no_native_startup_record(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);rpx=root/'bad.rpx'
            original=bytearray(self.files['code/ctr2wup.rpx'])
            struct.pack_into('>I',original,96,0);rpx.write_bytes(original)
            with self.assertRaises(HostError):
                stage_title(rpx,root/'title',title_id='0005000043543257')
            self.assertFalse((root/'title').exists())

    def test_movie_is_required_even_when_every_xml_and_image_is_valid(self):
        movie=self.files['meta/bootMovie.h264']
        for broken in (b'',movie[:-1],movie+b'\0',bytes([movie[0]^1])+movie[1:],b'not H.264'):
            with self.subTest(bytes=len(broken)),self.assertRaises(HostError):
                verify_menu_profile({**self.files,'meta/bootMovie.h264':broken})
        with self.assertRaises(HostError):
            verify_menu_profile({p:d for p,d in self.files.items() if p!='meta/bootMovie.h264'})

    def test_old_metadata_shape_and_each_missing_acp_field_are_rejected(self):
        doc=ET.fromstring(self.files['meta/meta.xml'])
        for node in doc:
            broken=copy.deepcopy(doc);broken.remove(broken.find(node.tag))
            with self.subTest(field=node.tag),self.assertRaises(HostError):
                verify_menu_profile({**self.files,'meta/meta.xml':ET.tostring(broken)})

    def test_cos_type_invalid_core_and_metadata_identity_fail_independently(self):
        for path,field,attribute,value in (
                ('code/cos.xml','cmdFlags','type','hexBinary'),
                ('code/cos.xml','codegen_core',None,'80000000'),
                ('meta/meta.xml','group_id',None,'00000400'),
                ('meta/meta.xml','title_id',None,'0005000043543258'),
                ('meta/meta.xml','title_version',None,'27'),
                ('meta/meta.xml','content_platform',None,'OTHER'),
                ('meta/meta.xml','drc_use',None,'0'),
                ('meta/meta.xml','invisible_flag',None,'00000001'),
                ('meta/meta.xml','no_managed_flag',None,'00000001'),
                ('meta/meta.xml','longname_en','length','8')):
            doc=ET.fromstring(self.files[path]);node=doc.find(field)
            if attribute:node.set(attribute,value)
            else:node.text=value
            with self.subTest(field=field),self.assertRaises(HostError):
                verify_menu_profile({**self.files,path:ET.tostring(doc)})

    def test_all_artwork_requires_dimensions_depth_origin_and_complete_footer(self):
        for path in [p for p in self.files if p.endswith('.tga')]:
            data=self.files[path]
            for broken in (data[:-26],data[:-1],data+b'\0',data[:2]+b'\x0a'+data[3:],
                           data[:12]+b'\x01\0'+data[14:],data[:17]+bytes([data[17]^32])+data[18:]):
                with self.subTest(path=path),self.assertRaises(HostError):
                    verify_menu_profile({**self.files,path:broken})
