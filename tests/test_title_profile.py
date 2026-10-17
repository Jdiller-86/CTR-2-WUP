"""Menu compatibility checks must catch defects that a hash roundtrip cannot."""
import copy
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from host.errors import HostError
from host.title_stage import stage_title
from host.title_profile import verify_menu_profile
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
        self.assertEqual(verify_menu_profile(self.files),'wup-menu-v1')
        self.assertEqual(len(ET.fromstring(self.files['meta/meta.xml'])),137)

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
