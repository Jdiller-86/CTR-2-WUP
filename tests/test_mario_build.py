"""Basic intake policy and generated native content identity."""
import copy
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

from host.errors import HostError
from host.loader import load_image
from host.mario_build import _embedded_audio_firmware,generated_image,mario_profile,title_identity
from tests.romfs_fixtures import ncch_romfs


def original_rom():
    # mov r0,#0; svc ExitProcess. Remaining bytes are original zero padding.
    return ncch_romfs(code=struct.pack('<II',0xe3a00000,0xef000003)+bytes(4088))


class MarioBuildTests(unittest.TestCase):
    def test_embedded_dsp_profile_requires_complete_segment_hashes(self):
        sample=bytearray(0x30a)
        struct.pack_into('<4sIH',sample,0x100,b'DSP1',len(sample),0xdf03)
        sample[0x10d:0x110]=bytes((2,5,3))
        struct.pack_into('<II',sample,0x110,0x200,2)
        for index,bank in enumerate((0,0,2,2,2)):
            table=0x120+index*0x30
            offset=0x300+index*2
            payload=bytes((index,0x7a))
            sample[offset:offset+2]=payload
            struct.pack_into('<IIIxxxB',sample,table,offset,index,2,bank)
            sample[table+16:table+48]=hashlib.sha256(payload).digest()
        self.assertEqual(_embedded_audio_firmware(sample),(len(sample),hashlib.sha256(sample).digest()))
        sample[-1]^=1
        self.assertEqual(_embedded_audio_firmware(sample),(0,bytes(32)))

    def test_profile_policy(self):
        selected={'identity':{'program_id':'0004000000054000'},'executable_revision':'original-test',
            'partitions':[{'product_code':'CTR-P-AREE','remaster_version':0,'sections':{'romfs':{}}}]}
        profile=mario_profile(selected)
        self.assertEqual(profile['compatibility'],'experimental')
        self.assertFalse(profile['gameplay_verified'])
        for key,value in (('product_code','CTR-P-TEST'),('remaster_version',1),('sections',{})):
            invalid=copy.deepcopy(selected);invalid['partitions'][0][key]=value
            with self.subTest(key=key),self.assertRaises(HostError):mario_profile(invalid)
        invalid=copy.deepcopy(selected);invalid['identity']['program_id']='000400000ff00f00'
        with self.assertRaises(HostError):mario_profile(invalid)

    def test_generated_image_binds_installed_content(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'original.cxi';path.write_bytes(original_rom());image=load_image(path)
            profile={'name':'Original test','program_id':image['inspection']['identity']['program_id']}
            title=title_identity(profile['program_id'])
            header,source=generated_image(image,profile,title,'ab'*32)
            self.assertIn(title,header)
            self.assertIn(image['inspection']['executable_revision'],header)
            self.assertIn('CTR_GAME_HAS_ROMFS 1',header)
            self.assertIn('NULL,4096u',source)
            self.assertIn(','.join('0x%02x'%b for b in hashlib.sha256(image['code']).digest()),source)
            self.assertIn(','.join(['0xab']*32),source)


if __name__=='__main__':unittest.main()
