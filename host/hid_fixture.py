"""Original ARM consumer of standard SRV/HID IPC, shared memory and events."""
import hashlib
import struct
from .guest_fixture import Program
TEXT_ADDRESS = 0x00100000
DATA_ADDRESS = 0x08000000
PROGRAM_ID = 0x000400000FF00F07

def hid_startup(p):
    p.constant(7,DATA_ADDRESS)
    p.emit(0xEE1DAF70)  # TLS -> r10
    p.emit(0xE28AA080)
    p.constant(1,DATA_ADDRESS+0x300)
    p.emit(0xEF00002D)
    p.emit(0xE1A04001)
    def word(index,value):
        p.constant(0,value);p.emit(0xE58A0000|index*4)
    def request(handle):
        p.emit(0xE1A00000|handle);p.emit(0xEF000032)
    word(0,0x10002);word(1,0x20);word(2,0);request(4)
    word(0,0x50100);word(1,0x3a646968);word(2,0x52455355);word(3,8);word(4,1);request(4)
    p.emit(0xE59A500C)
    word(0,0xa0000);request(5)
    p.emit(0xE59A600C);p.emit(0xE59A8010)
    for index in range(6):
        p.emit(0xE59A0000|(12+index*4));p.emit(0xE5870000|(0x200+index*4))
    p.emit(0xE1A00006);p.constant(1,0x10001000);p.constant(2,1);p.constant(3,0x10000000)
    p.emit(0xEF00001F)
    p.constant(9,0x10001000)
    return word,request

def hid_code():
    p=Program();hid_startup(p)
    p.label('sample')
    p.emit(0xE1A00008);p.constant(2,0xffffffff);p.constant(3,0xffffffff);p.emit(0xEF000024)
    p.emit(0xE5970100);p.emit(0xE3500000);p.branch('exit',1)
    p.emit(0xE5993010);p.emit(0xE0893203) # add r3,r9,r3,lsl#4
    p.emit(0xE5930028);p.emit(0xE5870000)
    p.emit(0xE5930034);p.emit(0xE5870004)
    p.emit(0xE59930B8);p.emit(0xE0893183) # add r3,r9,r3,lsl#3
    p.emit(0xE59300C8);p.emit(0xE5870008)
    p.emit(0xE59300CC);p.emit(0xE587000C)
    p.emit(0xE5970010);p.emit(0xE2800001);p.emit(0xE5870010);p.branch('sample')
    p.label('exit')
    p.emit(0xE1A00006);p.emit(0xE1A01009);p.emit(0xEF000020)
    p.constant(11,0)
    p.label('close_handles')
    p.emit(0xE087010B);p.emit(0xE5900200);p.emit(0xEF000023)
    p.emit(0xE28BB001);p.emit(0xE35B0006);p.branch('close_handles',1)
    p.emit(0xE1A00005);p.emit(0xEF000023);p.emit(0xE1A00004);p.emit(0xEF000023);p.emit(0xEF000003)
    return p.finish()

def image_code():
    code=bytearray(8192);text=hid_code();code[:len(text)]=text;code[4096+0x300:4096+0x305]=b'srv:\0'
    return bytes(code)

def fixture_bytes():
    from .heap_fixture import fixture_bytes as template
    out=bytearray(template());code=image_code()
    for at in (0x108,0x118):struct.pack_into('<Q',out,at,PROGRAM_ID)
    out[0x150:0x160]=b'CTR-P-HIDTEST'.ljust(16,b'\0');out[0x200:0x208]=b'CTRHID\0\0'
    struct.pack_into('<I',out,0x218,len(hid_code()));out[0x450:0x458]=b'hid:USER'
    out[0xc00:]=code;out[0xbe0:0xc00]=hashlib.sha256(code).digest()
    out[0x160:0x180]=hashlib.sha256(out[0x200:0x600]).digest();out[0x1c0:0x1e0]=hashlib.sha256(out[0xa00:0xc00]).digest()
    return bytes(out)

def generated_sources():
    from translator.aot import translate
    code=image_code();blob=',\n'.join('    '+','.join(f'0x{b:02x}' for b in code[i:i+32]) for i in range(0,len(code),32))
    metadata='#include "ctr_hid_fixture.h"\n'+f'static const uint8_t hid_image_bytes[]={{\n{blob}\n}};\n'
    metadata+='const ctr_guest_image ctr_hid_image={hid_image_bytes,sizeof(hid_image_bytes),\n'
    metadata+=f'    {{0x{TEXT_ADDRESS:08x}u,1u,{len(hid_code())}u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},\n'
    metadata+=f'    0,16384u,"{hashlib.sha256(code).hexdigest()}"}};\n'
    return {'hid.generated.c':translate(hid_code(),TEXT_ADDRESS,'ctr_hid_fixture',False),'hid-image.generated.c':metadata}
