import unittest
from renderer.transfer import convert,layout

class TransferTests(unittest.TestCase):
    def test_color_goldens(self):
        data=bytes((0x81,0x56,0xab,0xe3))*64;dim=8|(8<<16)
        expected=(bytes((0x81,0x56,0xab,0xe3)),bytes((0x56,0xab,0xe3)),bytes((0x4a,0xe5)),bytes((0x55,0xe5)),bytes((0x58,0xea)))
        for format in range(5):self.assertEqual(convert(data,dim,dim,0x22|(format<<12)),expected[format]*64)

    def test_all_formats_layouts_and_flip_roundtrip(self):
        dim=16|(8<<16)
        for format in range(5):
            stride=4 if format==0 else 3 if format==1 else 2;data=bytes((i*19)%256 for i in range(128*stride))
            for flip in (0,1):
                tiled=convert(data,dim,dim,2|flip|(format<<8)|(format<<12))
                back=convert(tiled,dim,dim,flip|(format<<8)|(format<<12))
                self.assertEqual(data,back)

    def test_morton_and_vertical_flip(self):
        dim=8|(8<<16);source=b''.join(bytes((255,0,0,x+y*8)) for y in range(8) for x in range(8))
        tiled=convert(source,dim,dim,2)
        self.assertEqual([tiled[i*4+3] for i in (0,1,4,5,16,17,20,21)],list(range(8)))
        flipped=convert(source,dim,dim,0x23)
        self.assertEqual(flipped[:32],source[-32:]);self.assertEqual(flipped[-32:],source[:32])

    def test_linear_small_dimensions_and_rejections(self):
        dim=1|(1<<16);self.assertEqual(convert(bytes((1,2,3,4)),dim,dim,0x22),bytes((1,2,3,4)))
        for flag in (0,8,0x10,0x1000000,0x500,0x5000):
            with self.assertRaises(ValueError):layout(dim,dim,flag)
        dim=8|(8<<16)
        with self.assertRaises(ValueError):convert(bytes(255),dim,dim,0)
        with self.assertRaises(ValueError):layout(dim,8|(16<<16),0)
        with self.assertRaises(ValueError):layout(dim,dim,True)
