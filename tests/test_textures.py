import struct
import unittest
from renderer.texture import decode_texture,required_bytes

class TextureTests(unittest.TestCase):
    def test_format_goldens(self):
        vectors=((0,b'\1\2\3\4',(4,3,2,1)),(1,b'\1\2\3',(3,2,1,255)),
                 (2,b'\1\xfc',(255,132,0,255)),(3,b'\0\xf8',(255,0,0,255)),
                 (4,b'\xcd\xab',(170,187,204,221)),(5,bytes((22,33)),(33,33,33,22)),
                 (6,bytes((22,33)),(33,22,0,255)),(7,b'@',(64,64,64,255)),
                 (8,b'\x80',(0,0,0,128)),(9,b'\xab',(170,170,170,187)),
                 (10,b'\x92',(34,34,34,255)),(11,b'\x92',(0,0,0,34)))
        for format,value,rgba in vectors:
            with self.subTest(format=format):
                data=(value*64)[:required_bytes(format,8,8)]
                result=decode_texture(data,format,8,8)
                self.assertEqual(result[:4],bytes(rgba))
                if format<10:self.assertEqual(result,bytes(rgba)*64)
                else:self.assertEqual(result[4:8],bytes((153,153,153,255) if format==10 else (0,0,0,153)))

    def test_morton_and_tile_grid(self):
        result=decode_texture(bytes(range(256)),7,16,16)
        for tile in range(4):
            for x,index in enumerate((0,1,4,5,16,17,20,21)):
                at=((tile//2)*8*16+(tile%2)*8+x)*4
                self.assertEqual(result[at:at+4],bytes((tile*64+index,)*3+(255,)))
            for y,index in enumerate((0,2,8,10,32,34,40,42)):
                at=(((tile//2)*8+y)*16+(tile%2)*8)*4
                self.assertEqual(result[at:at+4],bytes((tile*64+index,)*3+(255,)))

    def test_etc_individual_flip_and_alpha(self):
        for format in (12,13):
            for flip in (0,1):
                alpha=struct.pack('<Q',0xfedcba9876543210) if format==13 else b''
                data=(alpha+struct.pack('<II',0,0x123456e0|flip))*4
                result=decode_texture(data,format,8,8)
                for y in range(8):
                    for x in range(8):
                        left=y%4<2 if flip else x%4<2
                        expected=(64,98,132) if left else (36,70,104)
                        a=((x%4)*4+y%4)*17 if format==13 else 255
                        self.assertEqual(result[(y*8+x)*4:(y*8+x+1)*4],bytes(expected+(a,)))

    def test_etc_selector_planes_and_clamps(self):
        block=struct.pack('<II',0xffff0000,0x000000fc)
        self.assertEqual(decode_texture(block*4,12,8,8),bytes((0,0,0,255))*64)
        block=struct.pack('<II',0x0000ffff,0xfffffffc)
        self.assertEqual(decode_texture(block*4,12,8,8),bytes((255,255,255,255))*64)

    def test_etc_differential_and_negative_delta(self):
        # R 20-4, G 10+3, B 5-1, table 0 and +2.
        high=((20<<3|4)<<24)|((10<<3|3)<<16)|((5<<3|7)<<8)|2
        result=decode_texture(struct.pack('<II',0,high)*4,12,8,8)
        self.assertEqual(result[:4],bytes((167,84,43,255)))
        self.assertEqual(result[3*4:4*4],bytes((134,109,35,255)))
        for high in (0xf9000002,0x04000002,0x0000f902):
            with self.assertRaisesRegex(ValueError,'differential'):
                decode_texture(struct.pack('<II',0,high)*4,12,8,8)

    def test_dimensions_formats_and_truncation(self):
        for format in range(14):
            self.assertEqual(len(decode_texture(bytes(required_bytes(format,8,8)),format,8,8)),256)
            with self.assertRaises(ValueError):decode_texture(b'',format,8,8)
        for format in (-1,14,True,1.0):
            with self.assertRaises(ValueError):required_bytes(format,8,8)
        for dim in (0,4,7,9,1025,True,8.0):
            with self.assertRaises(ValueError):required_bytes(0,dim,8)
