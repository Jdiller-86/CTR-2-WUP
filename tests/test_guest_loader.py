import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

from host.guest_fixture import fixture_bytes, guest_code
from host.guest_pipeline import generated_sources, profiled_image
from host.loader import load_image
from host.errors import HostError


class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.path=Path(self.tmp.name)/"guest 日本語.cxi"
        self.path.write_bytes(fixture_bytes())

    def tearDown(self): self.tmp.cleanup()

    def mutate_header(self,offset,value):
        data=bytearray(self.path.read_bytes())
        struct.pack_into("<I",data,offset,value)
        data[0x160:0x180]=hashlib.sha256(data[0x200:0x600]).digest()
        self.path.write_bytes(data)

    def rejected(self,code):
        with self.assertRaises(HostError) as ctx: load_image(self.path)
        self.assertEqual(ctx.exception.code,code)

    def test_real_page_layout(self):
        image=profiled_image(self.path)
        self.assertEqual(image["text"],guest_code())
        self.assertEqual(image["allocated_bytes"],724992)
        self.assertEqual(image["tls_address"],0x1ff82000)

    def test_source_reproducibility(self):
        for name,contents in generated_sources(profiled_image(self.path)).items():
            self.assertEqual(contents,(Path(__file__).parent/"fixtures/guest"/name).read_text())

    def test_stack_alignment(self):
        self.mutate_header(0x21c,1); self.rejected("invalid_stack")

    def test_segment_alignment(self):
        self.mutate_header(0x210,0x100001); self.rejected("invalid_segment")

    def test_segment_page_mismatch(self):
        self.mutate_header(0x214,2); self.rejected("invalid_segment")

    def test_guest_segment_overlap(self):
        self.mutate_header(0x230,0x100000); self.rejected("overlapping_regions")

    def test_tls_overlap(self):
        self.mutate_header(0x230,0x1ff82000); self.rejected("overlapping_regions")

    def test_bss_starts_inside_last_data_page(self):
        self.mutate_header(0x238,4095)
        image=load_image(self.path)
        self.assertEqual(image["allocated_bytes"],724992)
        self.assertEqual(image["segments"]["data"]["size"],4095)

    def test_bss_can_fit_in_data_padding(self):
        self.mutate_header(0x238,16);self.mutate_header(0x23c,32)
        self.assertEqual(load_image(self.path)["allocated_bytes"],32768)

    def test_bss_range_must_not_overlap_tls(self):
        self.mutate_header(0x230,0x1ff81000);self.mutate_header(0x23c,4096)
        self.rejected("overlapping_regions")

    def test_memory_limit(self):
        self.mutate_header(0x23c,128*1024*1024); self.rejected("guest_memory_limit")

    def test_unrecognized_profile(self):
        self.mutate_header(0x21c,0x8000)
        with self.assertRaises(HostError) as ctx: profiled_image(self.path)
        self.assertEqual(ctx.exception.code,"unsupported_fixture_profile")

    def test_profile_rejects_truncated_text_descriptor(self):
        self.mutate_header(0x218,4)
        with self.assertRaises(HostError) as ctx: profiled_image(self.path)
        self.assertEqual(ctx.exception.code,"unsupported_fixture_profile")


if __name__=="__main__": unittest.main()
