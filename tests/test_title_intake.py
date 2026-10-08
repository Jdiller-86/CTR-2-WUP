import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from host.containers import inspect
from host.errors import HostError
from host.title_analysis import analyze_title
from host.title_stage import stage_title
from tests.host_fixtures import ncch


class TitleIntakeTests(unittest.TestCase):
    def test_allowlist_extra_slots_and_no_compatibility_enable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);path=root/'original.cxi';data=ncch()
            for slot,name in ((0,b'srv:'),(1,b'hid:USER'),(33,b'gsp::Gpu')):
                start=0x450+slot*8;data[start:start+8]=name.ljust(8,b'\0')
            data[0x160:0x180]=hashlib.sha256(data[0x200:0x600]).digest();path.write_bytes(data)
            result=analyze_title(path,root/'private intake 日本語')
            self.assertEqual(result['declared_services'],['srv:','hid:USER','gsp::Gpu'])
            self.assertFalse(result['profile_enabled']);self.assertEqual(result['compatibility'],'unsupported')
            self.assertEqual(json.loads((root/'private intake 日本語/requirements-report.json').read_text())['input_sha256'],hashlib.sha256(data).hexdigest())
            with self.assertRaises(HostError):analyze_title(path,root/'private intake 日本語')

    def test_corrupted_service_name_rejected_after_hash_verification(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'original.cxi';data=ncch();data[0x450]=255
            data[0x160:0x180]=hashlib.sha256(data[0x200:0x600]).digest();path.write_bytes(data)
            with self.assertRaises(HostError) as caught:inspect(path)
            self.assertEqual(caught.exception.code,'invalid_service_access')

    def test_system_title_cannot_be_staged(self):
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/'never published'
            for title in ('000500101000400a','0005000000000000','bad'):
                with self.assertRaises(HostError) as caught:stage_title('unused.rpx',output,title_id=title)
                self.assertEqual(caught.exception.code,'invalid_title_id')
                self.assertFalse(output.exists())
    def test_invalid_xml_and_overlong_utf8_title_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            for name in ('bad\ud800','bad\uffff','bad\x01','\U0001f600'*80):
                with self.assertRaises(HostError) as caught:
                    stage_title('unused.rpx',Path(temporary)/'never published',title_id='00050000cf520001',name=name)
                self.assertEqual(caught.exception.code,'invalid_title_metadata')

if __name__=='__main__':unittest.main()
